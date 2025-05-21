#pragma once

#include "async_simple/coro/Lazy.h"
#include <string>
#include <vector>

namespace tgdb {

struct Content {
  std::string text;
  std::string image_path;
  std::string video_path;
};

struct Embedding {
  int index;
  std::vector<float> embedding;
  std::string type; 
};

struct EmbeddingService {
  virtual ~EmbeddingService() = default;
  virtual std::string get_id() const = 0;
  virtual async_simple::coro::Lazy<std::vector<Embedding>>
  multimodal_embedding(const std::vector<Content>& contents) = 0;
  virtual bool support_aligned_image() const = 0;
};
} 