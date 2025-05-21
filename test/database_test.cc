#include "../src/database/database.hpp"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"
#include <chrono>
#include <filesystem>
#include <random>
#include <string_view>
#include <thread>

struct TestData {
  int id;
  std::string name;
  bool operator==(const TestData &other) const = default;
};

TEST(DatabaseTest, OpenAndClose) {
  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "tgdb_test_db";
  std::filesystem::remove_all(temp_dir);

  {
    kvdb::database db(temp_dir.string());
    auto open_result = db.open();
    ASSERT_TRUE(open_result.has_value()) << open_result.error();
    // Database should be open here
  }

  // Check if the directory exists (RocksDB creates files in it)
  ASSERT_TRUE(std::filesystem::exists(temp_dir));

  std::filesystem::remove_all(temp_dir);
  ASSERT_FALSE(std::filesystem::exists(temp_dir));
}

TEST(DatabaseTest, PutAndGetRaw) {
  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "tgdb_test_db_raw";
  std::filesystem::remove_all(temp_dir);

  {
    kvdb::database db(temp_dir.string());
    db.open();
    std::string key = "test_key";
    std::string value = "test_value";

    bool put_success = db.put_raw(key, value);
    ASSERT_TRUE(put_success);

    auto retrieved_value = db.get_raw(key);
    ASSERT_TRUE(retrieved_value.has_value()) << retrieved_value.error();
    ASSERT_EQ(retrieved_value.value(), value);

    auto non_existent_value = db.get_raw("non_existent_key");
    ASSERT_FALSE(non_existent_value.has_value());

  }

  std::filesystem::remove_all(temp_dir);
  ASSERT_FALSE(std::filesystem::exists(temp_dir));
}

TEST(DatabaseTest, PutAndGetSerialized) {
  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "tgdb_test_db_serialized";
  std::filesystem::remove_all(temp_dir);

  {
    kvdb::database<TestData> db(temp_dir.string());
    db.open();
    std::string key = "test_struct_key";
    TestData original_data = {123, "test_name"};

    bool put_success = db.put(key, original_data);
    ASSERT_TRUE(put_success);

    auto retrieved_data = db.get(key);
    ASSERT_TRUE(retrieved_data.has_value()) << retrieved_data.error();
    ASSERT_EQ(retrieved_data.value(), original_data);

    auto non_existent_data = db.get("non_existent_struct_key");
    ASSERT_FALSE(non_existent_data.has_value());

  }

  std::filesystem::remove_all(temp_dir);
  ASSERT_FALSE(std::filesystem::exists(temp_dir));
}

TEST(DatabaseTest, Remove) {
  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "tgdb_test_db_remove";
  std::filesystem::remove_all(temp_dir);

  {
    kvdb::database db(temp_dir.string());
    db.open();
    std::string key = "key_to_remove";
    std::string value = "value_to_remove";

    bool put_success = db.put_raw(key, value);
    ASSERT_TRUE(put_success);
    auto retrieved_value_before_remove = db.get_raw(key);
    ASSERT_TRUE(retrieved_value_before_remove.has_value())
        << retrieved_value_before_remove.error();

    auto remove_result = db.remove(key);
    ASSERT_TRUE(remove_result.has_value()) << remove_result.error();

    auto retrieved_value_after_remove = db.get_raw(key);
    ASSERT_FALSE(retrieved_value_after_remove.has_value());

  }

  std::filesystem::remove_all(temp_dir);
  ASSERT_FALSE(std::filesystem::exists(temp_dir));
}

TEST(DatabaseTest, Transaction) {
  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "tgdb_test_db_transaction";
  std::filesystem::remove_all(temp_dir);

  {
    kvdb::database db(temp_dir.string());
    auto open_result = db.open();
    ASSERT_TRUE(open_result.has_value()) << open_result.error();
    std::string key1 = "tx_key1";
    std::string value1 = "tx_value1";
    std::string key2 = "tx_key2";
    std::string value2 = "tx_value2";

    db.with_transaction([&](auto &tx) {
      tx.put_raw(key1, value1);
      tx.put_raw(key2, value2);
    });
    auto get_key1_result = db.get_raw(key1);
    ASSERT_TRUE(get_key1_result.has_value()) << get_key1_result.error();
    auto get_key2_result = db.get_raw(key2);
    ASSERT_TRUE(get_key2_result.has_value()) << get_key2_result.error();
  }

  std::filesystem::remove_all(temp_dir);
  ASSERT_FALSE(std::filesystem::exists(temp_dir));
}

static void BM_DatabaseIteratorBenchmark(benchmark::State &state) {
  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "tgdb_benchmark_db";
  std::filesystem::remove_all(temp_dir);

  std::filesystem::create_directories(temp_dir);

  {
    kvdb::database<std::string> db(temp_dir.append("test.db").string());
    auto open_result = db.open();
    if (!open_result.has_value()) {
      state.SkipWithError(
          ("Failed to open database: " + open_result.error()).c_str());
      return;
    }

    const int num_elements = 400000;
    std::mt19937 rng(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<int> dist(0, num_elements * 10);

    for (int i = 0; i < num_elements; ++i) {
      std::string key = "key_" + std::to_string(dist(rng));
      std::string value = "value_12e2ed12d12qd" + std::to_string(dist(rng));
      db.put(key, value);
    }

    for (auto _ : state) {
      for (const auto &[key, value] : db) {
        benchmark::DoNotOptimize(value.contains("abcdefgh"));
      }
    }
  }

  std::filesystem::remove_all(temp_dir);
}

BENCHMARK(BM_DatabaseIteratorBenchmark)->Unit(benchmark::kMillisecond);

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  benchmark::Initialize(&argc, argv);
  if (::testing::GTEST_FLAG(filter) ==
       "*") {
    benchmark::RunSpecifiedBenchmarks();
  }
  return RUN_ALL_TESTS();
}