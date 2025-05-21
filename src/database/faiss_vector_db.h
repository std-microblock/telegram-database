#pragma once

#include "vector_db.h"
#include <faiss/Index.h>
#include <faiss/IndexFlat.h>
#include <faiss/index_io.h>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace tgdb {

class FaissVectorDbService : public VectorDbService {
public:
    FaissVectorDbService(int dimension, faiss::MetricType metric = faiss::METRIC_L2);
    ~FaissVectorDbService() override;

    bool AddVector(const std::string& key, const std::vector<float>& vector) override;
    std::vector<SearchResult> Search(const std::vector<float>& query_vector, int top_k) override;
    bool RemoveVector(const std::string& key) override;
    bool UpdateVector(const std::string& key, const std::vector<float>& vector) override;
    bool Save(const std::string& path) override;
    bool Load(const std::string& path) override;

private:
    int dimension_;
    std::unique_ptr<faiss::Index> index_;
    
    
    std::unordered_map<std::string, faiss::Index::idx_t> key_to_id_;
    std::vector<std::string> id_to_key_; 
    faiss::Index::idx_t next_id_ = 0;
    std::mutex mutex_; 

    
    void RebuildIndex();
};

} 