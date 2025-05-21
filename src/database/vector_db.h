#pragma once

#include <string>
#include <vector>

namespace tgdb {

struct SearchResult {
  std::string key;
  float score;
};

class VectorDbService {
public:
  virtual ~VectorDbService() = default;

  virtual bool AddVector(const std::string &key,
                         const std::vector<float> &vector) = 0;

  virtual std::vector<SearchResult>
  Search(const std::vector<float> &query_vector, int top_k) = 0;
  virtual bool RemoveVector(const std::string &key) = 0;

  virtual bool UpdateVector(const std::string &key,
                            const std::vector<float> &vector) = 0;

  virtual bool Save(const std::string &path) = 0;

  virtual bool Load(const std::string &path) = 0;

  virtual bool CreateOrLoad(const std::string &path) = 0;
};

} // namespace tgdb