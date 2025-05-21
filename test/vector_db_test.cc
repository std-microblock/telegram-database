#include "gtest/gtest.h"
#include "faiss_vector_db.h"
#include "vector_db.h"
#include <filesystem>
#include <vector>
#include <string>
#include <numeric>


std::vector<float> CreateDummyVector(int dim, float start_val = 0.0f, float step = 1.0f) {
    std::vector<float> vec(dim);
    std::iota(vec.begin(), vec.end(), start_val);
    for(size_t i = 0; i < vec.size(); ++i) {
        vec[i] = start_val + i * step;
    }
    return vec;
}

class VectorDbTest : public ::testing::Test {
protected:
    const int dimension_ = 4; 
    const std::string test_db_path_ = "test_vector_db.faiss";
    std::unique_ptr<tgdb::VectorDbService> db_service_;

    void SetUp() override {
        
        std::filesystem::remove(test_db_path_);
        db_service_ = std::make_unique<tgdb::FaissVectorDbService>(dimension_);
    }

    void TearDown() override {
        
        std::filesystem::remove(test_db_path_);
    }
};

TEST_F(VectorDbTest, Initialization) {
    ASSERT_NE(db_service_, nullptr);
}

TEST_F(VectorDbTest, AddAndSearchVector) {
    std::string key1 = "vec1";
    std::vector<float> vec1 = CreateDummyVector(dimension_, 1.0f);

    ASSERT_TRUE(db_service_->AddVector(key1, vec1));

    
    auto results = db_service_->Search(vec1, 1);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].key, key1);
    
    EXPECT_FLOAT_EQ(results[0].score, 0.0f);

    std::string key2 = "vec2";
    std::vector<float> vec2 = CreateDummyVector(dimension_, 5.0f);
    ASSERT_TRUE(db_service_->AddVector(key2, vec2));

    results = db_service_->Search(vec2, 1);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].key, key2);
    EXPECT_FLOAT_EQ(results[0].score, 0.0f);

    
    results = db_service_->Search(vec1, 1);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].key, key1);
    EXPECT_FLOAT_EQ(results[0].score, 0.0f);
}

TEST_F(VectorDbTest, SearchMultipleResults) {
    std::string key1 = "vec1";
    std::vector<float> vec1 = CreateDummyVector(dimension_, 1.0f); 
    std::string key2 = "vec2";
    std::vector<float> vec2 = CreateDummyVector(dimension_, 1.1f); 
    std::string key3 = "vec3";
    std::vector<float> vec3 = CreateDummyVector(dimension_, 10.0f); 

    ASSERT_TRUE(db_service_->AddVector(key1, vec1));
    ASSERT_TRUE(db_service_->AddVector(key2, vec2));
    ASSERT_TRUE(db_service_->AddVector(key3, vec3));

    
    std::vector<float> query_vec = CreateDummyVector(dimension_, 0.9f); 
    auto results = db_service_->Search(query_vec, 3);
    ASSERT_EQ(results.size(), 3);

    
    EXPECT_EQ(results[0].key, key1);
    EXPECT_EQ(results[1].key, key2);
    EXPECT_EQ(results[2].key, key3);
}

TEST_F(VectorDbTest, AddExistingKey) {
    std::string key1 = "vec_exist";
    std::vector<float> vec1 = CreateDummyVector(dimension_, 1.0f);
    std::vector<float> vec2 = CreateDummyVector(dimension_, 2.0f);

    ASSERT_TRUE(db_service_->AddVector(key1, vec1));
    
    
    ASSERT_FALSE(db_service_->AddVector(key1, vec2));

    
    auto results = db_service_->Search(vec1, 1);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].key, key1);
    EXPECT_FLOAT_EQ(results[0].score, 0.0f);
}

TEST_F(VectorDbTest, SearchEmptyDatabase) {
    std::vector<float> query_vec = CreateDummyVector(dimension_);
    auto results = db_service_->Search(query_vec, 1);
    ASSERT_TRUE(results.empty());
}

TEST_F(VectorDbTest, RemoveVector) {
    std::string key1 = "vec_to_remove";
    std::vector<float> vec1 = CreateDummyVector(dimension_, 1.0f);
    ASSERT_TRUE(db_service_->AddVector(key1, vec1));

    auto results_before_remove = db_service_->Search(vec1, 1);
    ASSERT_EQ(results_before_remove.size(), 1);

    ASSERT_TRUE(db_service_->RemoveVector(key1));

    auto results_after_remove = db_service_->Search(vec1, 1);
    ASSERT_TRUE(results_after_remove.empty());

    
    ASSERT_FALSE(db_service_->RemoveVector("non_existent_key"));
}

TEST_F(VectorDbTest, UpdateVector) {
    std::string key1 = "vec_to_update";
    std::vector<float> original_vec = CreateDummyVector(dimension_, 1.0f);
    std::vector<float> updated_vec = CreateDummyVector(dimension_, 10.0f);

    ASSERT_TRUE(db_service_->AddVector(key1, original_vec));

    
    auto results_original = db_service_->Search(original_vec, 1);
    ASSERT_EQ(results_original.size(), 1);
    EXPECT_EQ(results_original[0].key, key1);
    EXPECT_FLOAT_EQ(results_original[0].score, 0.0f);

    ASSERT_TRUE(db_service_->UpdateVector(key1, updated_vec));

    
    auto results_after_update_orig = db_service_->Search(original_vec, 1);
    ASSERT_EQ(results_after_update_orig.size(), 1); 
    EXPECT_NE(results_after_update_orig[0].score, 0.0f); 

    
    auto results_updated = db_service_->Search(updated_vec, 1);
    ASSERT_EQ(results_updated.size(), 1);
    EXPECT_EQ(results_updated[0].key, key1);
    EXPECT_FLOAT_EQ(results_updated[0].score, 0.0f);

    
    ASSERT_FALSE(db_service_->UpdateVector("non_existent_key", updated_vec));
}


TEST_F(VectorDbTest, SaveAndLoad) {
    std::string key1 = "saveload_vec1";
    std::vector<float> vec1 = CreateDummyVector(dimension_, 1.0f);
    std::string key2 = "saveload_vec2";
    std::vector<float> vec2 = CreateDummyVector(dimension_, 5.0f);

    ASSERT_TRUE(db_service_->AddVector(key1, vec1));
    ASSERT_TRUE(db_service_->AddVector(key2, vec2));

    ASSERT_TRUE(db_service_->Save(test_db_path_));

    
    auto new_db_service = std::make_unique<tgdb::FaissVectorDbService>(dimension_);
    ASSERT_TRUE(new_db_service->Load(test_db_path_));

    
    auto results = new_db_service->Search(vec1, 1);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].key, key1);
    EXPECT_FLOAT_EQ(results[0].score, 0.0f);

    results = new_db_service->Search(vec2, 2); 
    ASSERT_EQ(results.size(), 2);
    
    bool found_key1 = false;
    bool found_key2 = false;
    for(const auto& res : results) {
        if (res.key == key1) found_key1 = true;
        if (res.key == key2) found_key2 = true;
    }
    EXPECT_TRUE(found_key1);
    EXPECT_TRUE(found_key2);
}

TEST_F(VectorDbTest, LoadNonExistentFile) {
    ASSERT_FALSE(db_service_->Load("non_existent_db.faiss"));
}

TEST_F(VectorDbTest, AddVectorWithWrongDimension) {
    std::string key1 = "wrong_dim_vec";
    std::vector<float> wrong_dim_vec = CreateDummyVector(dimension_ + 1, 1.0f); 
    
    ASSERT_FALSE(db_service_->AddVector(key1, wrong_dim_vec));

    std::vector<float> also_wrong_dim_vec = CreateDummyVector(dimension_ -1 < 0 ? 1 : dimension_ -1 , 1.0f); 
    if (dimension_ > 1) { 
        ASSERT_FALSE(db_service_->AddVector("wrong_dim_vec2", also_wrong_dim_vec));
    }
}

TEST_F(VectorDbTest, SearchWithWrongDimension) {
    std::string key1 = "correct_dim_vec";
    std::vector<float> correct_dim_vec = CreateDummyVector(dimension_, 1.0f);
    ASSERT_TRUE(db_service_->AddVector(key1, correct_dim_vec));

    std::vector<float> wrong_dim_query = CreateDummyVector(dimension_ + 1, 1.0f);
    
    
    
    
    auto results = db_service_->Search(wrong_dim_query, 1);
    ASSERT_TRUE(results.empty());

    if (dimension_ > 1) {
        std::vector<float> also_wrong_dim_query = CreateDummyVector(dimension_ - 1, 1.0f);
        results = db_service_->Search(also_wrong_dim_query, 1);
        ASSERT_TRUE(results.empty());
    }
}

TEST_F(VectorDbTest, UpdateNonExistentKey) {
    std::vector<float> vec = CreateDummyVector(dimension_);
    ASSERT_FALSE(db_service_->UpdateVector("non_existent_key_update", vec));
}

TEST_F(VectorDbTest, UpdateVectorWithWrongDimension) {
    std::string key1 = "update_dim_test";
    std::vector<float> vec1 = CreateDummyVector(dimension_);
    ASSERT_TRUE(db_service_->AddVector(key1, vec1));

    std::vector<float> wrong_dim_vec = CreateDummyVector(dimension_ + 1);
    ASSERT_FALSE(db_service_->UpdateVector(key1, wrong_dim_vec));

    
    auto results = db_service_->Search(vec1, 1);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].key, key1);
}


TEST_F(VectorDbTest, RemoveAndReAdd) {
    std::string key = "readd_key";
    std::vector<float> vec = CreateDummyVector(dimension_, 7.0f);

    ASSERT_TRUE(db_service_->AddVector(key, vec));
    auto search_results1 = db_service_->Search(vec, 1);
    ASSERT_EQ(search_results1.size(), 1);
    EXPECT_EQ(search_results1[0].key, key);

    ASSERT_TRUE(db_service_->RemoveVector(key));
    auto search_results2 = db_service_->Search(vec, 1);
    ASSERT_TRUE(search_results2.empty());

    ASSERT_TRUE(db_service_->AddVector(key, vec)); 
    auto search_results3 = db_service_->Search(vec, 1);
    ASSERT_EQ(search_results3.size(), 1);
    EXPECT_EQ(search_results3[0].key, key);
    EXPECT_FLOAT_EQ(search_results3[0].score, 0.0f);
}

TEST_F(VectorDbTest, SearchTopKMoreThanItems) {
    std::string key1 = "item1";
    std::vector<float> vec1 = CreateDummyVector(dimension_, 1.0f);
    std::string key2 = "item2";
    std::vector<float> vec2 = CreateDummyVector(dimension_, 2.0f);

    ASSERT_TRUE(db_service_->AddVector(key1, vec1));
    ASSERT_TRUE(db_service_->AddVector(key2, vec2));

    auto results = db_service_->Search(vec1, 5); 
    ASSERT_EQ(results.size(), 2); 
    EXPECT_EQ(results[0].key, key1); 
}












int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}