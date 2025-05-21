#include "faiss_vector_db.h"
#include "faiss/impl/FaissException.h"
#include "ylt/easylog.hpp"
#include <faiss/IndexFlat.h>
#include <faiss/index_io.h>
#include <stdexcept>

namespace {
void write_map(
    const std::string &filename,
    const std::unordered_map<std::string, faiss::Index::idx_t> &map) {
  std::ofstream ofs(filename, std::ios::binary);
  if (!ofs) {
    throw std::runtime_error("Cannot open file for writing map: " + filename);
  }
  size_t map_size = map.size();
  ofs.write(reinterpret_cast<const char *>(&map_size), sizeof(map_size));
  for (const auto &pair : map) {
    size_t key_len = pair.first.length();
    ofs.write(reinterpret_cast<const char *>(&key_len), sizeof(key_len));
    ofs.write(pair.first.c_str(), key_len);
    ofs.write(reinterpret_cast<const char *>(&pair.second),
              sizeof(pair.second));
  }
}

void read_map(const std::string &filename,
              std::unordered_map<std::string, faiss::Index::idx_t> &map) {
  std::ifstream ifs(filename, std::ios::binary);
  if (!ifs) {
    throw std::runtime_error("Cannot open file for reading map: " + filename);
  }
  map.clear();
  size_t map_size;
  ifs.read(reinterpret_cast<char *>(&map_size), sizeof(map_size));
  if (ifs.fail())
    throw std::runtime_error("Failed to read map size from: " + filename);

  for (size_t i = 0; i < map_size; ++i) {
    size_t key_len;
    ifs.read(reinterpret_cast<char *>(&key_len), sizeof(key_len));
    if (ifs.fail())
      throw std::runtime_error("Failed to read key length from: " + filename);

    std::string key(key_len, '\0');
    ifs.read(&key[0], key_len);
    if (ifs.fail())
      throw std::runtime_error("Failed to read key from: " + filename);

    faiss::Index::idx_t id;
    ifs.read(reinterpret_cast<char *>(&id), sizeof(id));
    if (ifs.fail())
      throw std::runtime_error("Failed to read id from: " + filename);
    map[key] = id;
  }
}

void write_vector_of_strings(const std::string &filename,
                             const std::vector<std::string> &vec) {
  std::ofstream ofs(filename, std::ios::binary);
  if (!ofs) {
    throw std::runtime_error(
        "Cannot open file for writing vector of strings: " + filename);
  }
  size_t vec_size = vec.size();
  ofs.write(reinterpret_cast<const char *>(&vec_size), sizeof(vec_size));
  for (const auto &str : vec) {
    size_t str_len = str.length();
    ofs.write(reinterpret_cast<const char *>(&str_len), sizeof(str_len));
    ofs.write(str.c_str(), str_len);
  }
}

void read_vector_of_strings(const std::string &filename,
                            std::vector<std::string> &vec) {
  std::ifstream ifs(filename, std::ios::binary);
  if (!ifs) {
    throw std::runtime_error(
        "Cannot open file for reading vector of strings: " + filename);
  }
  vec.clear();
  size_t vec_size;
  ifs.read(reinterpret_cast<char *>(&vec_size), sizeof(vec_size));
  if (ifs.fail())
    throw std::runtime_error("Failed to read vector size from: " + filename);

  vec.resize(vec_size);
  for (size_t i = 0; i < vec_size; ++i) {
    size_t str_len;
    ifs.read(reinterpret_cast<char *>(&str_len), sizeof(str_len));
    if (ifs.fail())
      throw std::runtime_error("Failed to read string length from: " +
                               filename);

    std::string str(str_len, '\0');
    ifs.read(&str[0], str_len);
    if (ifs.fail())
      throw std::runtime_error("Failed to read string from: " + filename);
    vec[i] = str;
  }
}
} 

namespace tgdb {

FaissVectorDbService::FaissVectorDbService(int dimension,
                                           faiss::MetricType metric)
    : dimension_(dimension), next_id_(0) {
  if (metric == faiss::METRIC_L2) {
    index_ = std::make_unique<faiss::IndexFlatL2>(dimension_);
  } else if (metric == faiss::METRIC_INNER_PRODUCT) {
    index_ = std::make_unique<faiss::IndexFlatIP>(dimension_);
  } else {
    ELOGFMT(WARN, "Warning: Unsupported Faiss metric type, defaulting to METRIC_L2.");
    index_ = std::make_unique<faiss::IndexFlatL2>(dimension_);
  }
}

FaissVectorDbService::~FaissVectorDbService() = default;

bool FaissVectorDbService::AddVector(const std::string &key,
                                     const std::vector<float> &vector) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (key_to_id_.count(key)) {
    return false;
  }
  if (vector.size() != dimension_) {
    ELOGFMT(ERROR, "Error: Vector dimension mismatch for key '{}'. Expected {}, got {}.", key, dimension_, vector.size());
    return false;
  }

  faiss::Index::idx_t current_id = next_id_++;
  index_->add(1, vector.data());

  key_to_id_[key] = current_id;
  if (current_id >= id_to_key_.size()) {
    id_to_key_.resize(current_id + 1);
  }
  id_to_key_[current_id] = key;

  return true;
}

std::vector<SearchResult>
FaissVectorDbService::Search(const std::vector<float> &query_vector,
                             int top_k) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (query_vector.size() != dimension_) {
    ELOGFMT(ERROR, "Error: Query vector dimension mismatch. Expected {}, got {}.", dimension_, query_vector.size());
    return {};
  }
  if (index_->ntotal == 0) {
    return {};
  }
  if (top_k <= 0) {
    return {};
  }

  std::vector<faiss::Index::idx_t> result_ids(top_k);
  std::vector<float> result_distances(top_k);

  index_->search(1, query_vector.data(), top_k, result_distances.data(),
                 result_ids.data());

  std::vector<SearchResult> search_results;
  for (int i = 0; i < top_k; ++i) {
    if (result_ids[i] != -1 &&
        static_cast<size_t>(result_ids[i]) < id_to_key_.size()) {
      const std::string &key = id_to_key_[result_ids[i]];
      if (!key.empty()) {
        search_results.push_back({key, result_distances[i]});
      }
    }
  }
  return search_results;
}

bool FaissVectorDbService::RemoveVector(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = key_to_id_.find(key);
  if (it == key_to_id_.end()) {
    return false; 
  }

  key_to_id_.erase(it);
  RebuildIndex();

  return true;
}

void FaissVectorDbService::RebuildIndex() {
  if (dimension_ <= 0)
    return;

  std::unique_ptr<faiss::Index> new_index;
  if (index_->metric_type == faiss::METRIC_L2) {
    new_index = std::make_unique<faiss::IndexFlatL2>(dimension_);
  } else if (index_->metric_type == faiss::METRIC_INNER_PRODUCT) {
    new_index = std::make_unique<faiss::IndexFlatIP>(dimension_);
  } else {
    ELOGFMT(ERROR, "Error: RebuildIndex encountered unknown metric type. Defaulting to L2.");
    new_index = std::make_unique<faiss::IndexFlatL2>(dimension_);
  }

  std::vector<std::string> new_id_to_key;
  std::unordered_map<std::string, faiss::Index::idx_t> new_key_to_id;
  faiss::Index::idx_t current_new_id = 0;

  std::vector<float> buffer(dimension_);
  for (faiss::Index::idx_t old_id = 0; old_id < index_->ntotal; ++old_id) {
    if (static_cast<size_t>(old_id) < id_to_key_.size() &&
        !id_to_key_[old_id].empty()) {
      const std::string &key = id_to_key_[old_id];
      if (key_to_id_.count(
              key)) {
        try {
          index_->reconstruct(old_id, buffer.data());
          new_index->add(1, buffer.data());
          new_key_to_id[key] = current_new_id;
          if (current_new_id >= new_id_to_key.size()) {
            new_id_to_key.resize(current_new_id + 1);
          }
          new_id_to_key[current_new_id] = key;
          current_new_id++;
        } catch (const faiss::FaissException &e) {
          ELOGFMT(ERROR, "FaissException during reconstruct in RebuildIndex for id {}: {}", old_id, e.what());
        }
      }
    }
  }

  index_ = std::move(new_index);
  key_to_id_ = std::move(new_key_to_id);
  id_to_key_ = std::move(new_id_to_key);
  next_id_ = current_new_id; 
}

bool FaissVectorDbService::UpdateVector(const std::string &key,
                                        const std::vector<float> &vector) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (key_to_id_.find(key) == key_to_id_.end()) {
    return false;
  }
  if (vector.size() != dimension_) {
    ELOGFMT(ERROR, "Error: Vector dimension mismatch for update on key '{}'.", key);
    return false;
  }

  faiss::Index::idx_t old_faiss_id = key_to_id_[key];
  key_to_id_.erase(key);

  RebuildIndex();

  faiss::Index::idx_t new_id = next_id_++;
  index_->add(1, vector.data());
  key_to_id_[key] = new_id;
  if (new_id >= id_to_key_.size()) {
    id_to_key_.resize(new_id + 1);
  }
  id_to_key_[new_id] = key;

  return true;
}

bool FaissVectorDbService::Save(const std::string &path) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    faiss::write_index(index_.get(), (path + ".faissidx").c_str());
    write_map(path + ".key2id", key_to_id_);
    write_vector_of_strings(path + ".id2key", id_to_key_);

    std::ofstream meta_ofs(path + ".meta", std::ios::binary);
    if (!meta_ofs)
      return false;
    meta_ofs.write(reinterpret_cast<const char *>(&next_id_), sizeof(next_id_));
    meta_ofs.write(reinterpret_cast<const char *>(&dimension_),
                   sizeof(dimension_));
    faiss::MetricType metric = index_->metric_type;
    meta_ofs.write(reinterpret_cast<const char *>(&metric), sizeof(metric));
    meta_ofs.close();

    return true;
  } catch (const faiss::FaissException &e) {
    ELOGFMT(ERROR, "FaissException during Save: {}", e.what());
    return false;
  } catch (const std::exception &e) {
    ELOGFMT(ERROR, "StdException during Save: {}", e.what());
    return false;
  }
}

bool FaissVectorDbService::Load(const std::string &path) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    std::string index_file = path + ".faissidx";
    std::string key2id_file = path + ".key2id";
    std::string id2key_file = path + ".id2key";
    std::string meta_file = path + ".meta";

    
    std::ifstream index_ifs(index_file, std::ios::binary);
    if (!index_ifs) {
      ELOGFMT(ERROR, "Load error: Missing {}", index_file);
      return false;
    }
    index_ifs.close();

    std::ifstream key2id_ifs(key2id_file, std::ios::binary);
    if (!key2id_ifs) {
      ELOGFMT(ERROR, "Load error: Missing {}", key2id_file);
      return false;
    }
    key2id_ifs.close();

    std::ifstream id2key_ifs(id2key_file, std::ios::binary);
    if (!id2key_ifs) {
      ELOGFMT(ERROR, "Load error: Missing {}", id2key_file);
      return false;
    }
    id2key_ifs.close();

    std::ifstream meta_ifs(meta_file, std::ios::binary);
    if (!meta_ifs) {
      ELOGFMT(ERROR, "Load error: Missing {}", meta_file);
      return false;
    }

    index_.reset(faiss::read_index(index_file.c_str()));
    read_map(key2id_file, key_to_id_);
    read_vector_of_strings(id2key_file, id_to_key_);

    meta_ifs.read(reinterpret_cast<char *>(&next_id_), sizeof(next_id_));
    meta_ifs.read(reinterpret_cast<char *>(&dimension_), sizeof(dimension_));
    faiss::MetricType metric;
    meta_ifs.read(reinterpret_cast<char *>(&metric), sizeof(metric));
    meta_ifs.close();

    if (index_->d != dimension_) {
      ELOGFMT(ERROR, "Load Error: Index dimension mismatch. Expected {}, loaded index has {}", dimension_, index_->d);
      index_.reset();
      key_to_id_.clear();
      id_to_key_.clear();
      next_id_ = 0;
      if (this->dimension_ >
          0) {
        if (metric == faiss::METRIC_L2)
          index_ = std::make_unique<faiss::IndexFlatL2>(this->dimension_);
        else if (metric == faiss::METRIC_INNER_PRODUCT)
          index_ = std::make_unique<faiss::IndexFlatIP>(this->dimension_);
        else
          index_ =
              std::make_unique<faiss::IndexFlatL2>(this->dimension_);
      }
      return false;
    }

    return true;
  } catch (const faiss::FaissException &e) {
    ELOGFMT(ERROR, "FaissException during Load: {}", e.what());
    return false;
  } catch (const std::exception &e) {
    ELOGFMT(ERROR, "StdException during Load: {}", e.what());
    return false;
  }
}

bool FaissVectorDbService::CreateOrLoad(const std::string &path) {
  if (!Load(path)) {
    return Save(path);
  }
  return true;
}
} // namespace tgdb
