#pragma once

#include "async_simple/coro/Lazy.h"
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

namespace tgdb {

struct Content {
  std::string text;
  std::string image_path;
  std::string video_path;

  bool empty() const {
    return text.empty() && image_path.empty() && video_path.empty();
  }
};

enum class EmbeddingType {
  Text,
  Image
};

using Embedding = std::unordered_map<EmbeddingType, std::vector<float>>;

struct EmbeddingService {
  virtual ~EmbeddingService() = default;
  virtual std::string get_id() const = 0;
  virtual async_simple::coro::Lazy<std::expected<Embedding, std::string>>
  multimodal_embedding(Content contents) = 0;
  virtual bool support_aligned_image() const = 0;
};
} // namespace tgdb

namespace std {
template <> struct formatter<tgdb::Content> : std::formatter<std::string> {
  template <class FmtContext>
  FmtContext::iterator format(const tgdb::Content &content,
                              FmtContext &ctx) const {
    return std::format_to(ctx.out(),
                          "Content(text: {}, image_path: {}, video_path: {})",
                          content.text, content.image_path, content.video_path);
  }
};
} // namespace std