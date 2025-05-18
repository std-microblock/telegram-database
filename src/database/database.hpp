#pragma once
#include <expected>
#include <print>
#include <string>
#include <unordered_map>

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"

#include "ylt/easylog.hpp"
#include "ylt/struct_pack.hpp"

namespace kvdb {
template <typename T = std::string> struct database;

template <typename T> struct transaction_batch {
  rocksdb::WriteBatch batch;
  database<T> *db;

  transaction_batch(database<T> *db) : db(db) {}

  void put_raw(std::string_view key, std::string_view value) {
    batch.Put(key, value);
    if (db->cache) {
      db->cache->emplace(key, value);
    }
  }

  void put(std::string_view key, const T &value) {
    auto packed_value = struct_pack::serialize(value);
    put_raw(key, std::string_view(packed_value.data(), packed_value.size()));
  }

  void remove(std::string_view key) {
    batch.Delete(key);
    if (db->cache) {
      db->cache->erase(key);
    }
  }

  void commit();
};

template <typename T> struct database_iterator {
  rocksdb::Iterator *iter = nullptr;
  std::optional<typename std::map<std::string, T>::iterator> cache_iter;

  using iterator_category = std::input_iterator_tag;
  using value_type = std::pair<const std::string, T>;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type *;
  using reference = value_type &;

  database_iterator(rocksdb::DB *db,
                    rocksdb::ReadOptions options = rocksdb::ReadOptions()) {
    iter = db->NewIterator(options);
    iter->SeekToFirst();
  }

  database_iterator() : iter() {}

  database_iterator(rocksdb::Iterator *iterator) : iter(iterator) {}

  ~database_iterator() { delete iter; }

  bool valid() const { return iter && iter->Valid(); }

  mutable value_type current;
  reference operator*() {
    if (cache_iter) {
      return *cache_iter.value();
    }

    if (!valid()) {
      throw std::runtime_error("Iterator is not valid");
    }
    auto key = iter->key().ToString();
    auto packed_value = iter->value().ToString();
    auto value = struct_pack::deserialize<T>(packed_value);
    if (value) {
      // current = value_type{key, value.value()};
      return current;
    } else {
      throw std::runtime_error("Failed to deserialize value");
    }
  }
  value_type operator*() const {
    if (cache_iter) {
      return {cache_iter.value()->first, cache_iter.value()->second};
    }

    if (!valid()) {
      throw std::runtime_error("Iterator is not valid");
    }
    auto key = iter->key().ToString();
    auto packed_value = iter->value().ToString();
    auto value = struct_pack::deserialize<T>(packed_value);
    if (value) {
      return {key, *value};
    } else {
      throw std::runtime_error("Failed to deserialize value");
    }
  }

  database_iterator &operator++() {
    if (cache_iter) {
      ++(*cache_iter);
      return *this;
    } else {
      iter->Next();
      return *this;
    }
  }

  database_iterator operator++(int) {
    database_iterator temp = *this;
    ++(*this);
    return temp;
  }

  bool operator==(const database_iterator &other) const {
    if (cache_iter) {
      return cache_iter == other.cache_iter;
    }

    if (!this->valid() && !other.valid()) {
      return true;
    }
    if (!this->valid() || !other.valid()) {
      return false;
    }

    return (iter->key().compare(other.iter->key()) == 0 &&
            iter->value().compare(other.iter->value()) == 0);
  }

  bool operator!=(const database_iterator &other) const {
    return !(*this == other);
  }
};

template <typename T> struct database {
  rocksdb::DB *db;
  rocksdb::Options options;
  std::string db_path;

  std::optional<std::map<std::string, T>> cache = std::map<std::string, T>();

  database(std::string_view db_path) {
    options.create_if_missing = true;
    options.error_if_exists = false;

    this->db_path = db_path;
  }
  std::expected<void, std::string> open() {
    rocksdb::Status s = rocksdb::DB::Open(options, db_path, &db);

    if (s.ok()) {
      if (cache) {
        cache->clear();
        auto it = db->NewIterator(rocksdb::ReadOptions());
        it->SeekToFirst();
        for (; it->Valid(); it->Next()) {
          auto key = it->key().ToString();
          auto packed_value = it->value().ToString();
          auto value = struct_pack::deserialize<T>(packed_value);
          if (value) {
            cache->emplace(key, *value);
          } else {
            return std::unexpected("Failed to deserialize value: " +
                                   value.error());
          }
        }
      }
      return {};
    } else {
      return std::unexpected(s.ToString());
    }
  }
  ~database() { delete db; }
  std::expected<std::string, std::string> get_raw(std::string_view key) {
    std::string value;

    rocksdb::Status s = db->Get(rocksdb::ReadOptions(), key, &value);
    if (s.ok()) {
      return value;
    } else {
      return std::unexpected(s.ToString());
    }
  }
  bool has(std::string_view key) {
    if (cache) {
      auto it = cache->find(std::string(key));
      if (it != cache->end()) {
        return true;
      }
    } else {
      std::string value;
      rocksdb::Status s = db->Get(rocksdb::ReadOptions(), key, &value);
      return s.ok();
    }
  }
  bool put_raw(std::string_view key, std::string_view value) {
    rocksdb::Status s = db->Put(rocksdb::WriteOptions(), key, value);
    return s.ok();
  }

  bool put(std::string_view key, const T &value) {
    auto packed_value = struct_pack::serialize<std::string, T>(value);
    if (cache) {
      cache->emplace(key, value);
    }
    return put_raw(key, packed_value);
  }

  std::expected<T, std::string> get(std::string_view key) {
    if (cache) {
      auto it = cache->find(std::string(key));
      if (it != cache->end()) {
        return it->second;
      }
    }
    auto packed_value = get_raw(key);
    if (packed_value.has_value()) {
      if (auto val = struct_pack::deserialize<T>(packed_value.value()); val) {
        return *val;
      } else {
        return std::unexpected("Failed to deserialize value: " + val.error());
      }
    } else {
      return std::unexpected(packed_value.error());
    }
  }

  void with_transaction(
      std::function<void(transaction_batch<T> &)> transaction_func) {
    transaction_batch<T> batch(this);
    transaction_func(batch);
    batch.commit();
  }

  std::expected<void, std::string> remove(std::string_view key) {
    rocksdb::Status s = db->Delete(rocksdb::WriteOptions(), key);
    if (s.ok()) {
      if (cache) {
        cache->erase(std::string(key));
      }
      return {};
    } else {
      return std::unexpected(s.ToString());
    }
  }

  database_iterator<T> begin();
  database_iterator<T> end();
};
template <typename T> inline database_iterator<T> database<T>::end() {
  auto e = database_iterator<T>();
  if (cache) {
    e.cache_iter = cache->end();
  }
  return e;
}

template <typename T> inline database_iterator<T> database<T>::begin() {
  auto b = database_iterator<T>(db);
  if (cache) {
    b.cache_iter = cache->begin();
  }
  return b;
}

template <typename T> inline void transaction_batch<T>::commit() {
  rocksdb::Status s = db->db->Write(rocksdb::WriteOptions(), &batch);
  if (!s.ok()) {
    throw std::runtime_error("Transaction commit failed: " + s.ToString());
  }
}
}; // namespace kvdb