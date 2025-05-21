#pragma once

#include "embedding_service.h"

#include <string>
#include <vector>
#include <map>

namespace tgdb {

struct DashScopeEmbeddingRequest {
  std::string model;
  struct {
    std::vector<std::map<std::string, std::string>> contents;
  } input;
  struct {
    
  } parameters;
};

struct DashScopeEmbeddingResponse {
  struct Output {
    struct EmbeddingResult {
      int index;
      std::vector<float> embedding;
      std::string type;
    };
    std::vector<EmbeddingResult> embeddings;
  } output;

  struct Usage {
    int input_tokens;
    int image_count;
    float duration;
  } usage;

  std::string request_id;
};

class DashScopeEmbeddingService : public EmbeddingService {
public:
  DashScopeEmbeddingService(
      const std::string &api_key,
      const std::string &model_id = "multimodal-embedding-v1");

  std::string get_id() const override;

  async_simple::coro::Lazy<std::vector<Embedding>>
  multimodal_embedding(const std::vector<Content> &contents) override;

  bool support_aligned_image() const override;

  std::string api_key_;
  std::string model_id_;
  std::string endpoint_;
};

} 