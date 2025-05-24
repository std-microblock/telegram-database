#pragma once

#include "async_simple/coro/Collect.h"
#include "async_simple/coro/Lazy.h"
#include "embedding_service.h"

#include <map>
#include <string>
#include <vector>

#include "ylt/easylog.hpp"

#include "../utils.h"

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

  task_batch_debounce_pool<Content, Embedding> task_pool{
      std::chrono::milliseconds(1500),
      [this](std::vector<Content> contents)
          -> async_simple::coro::Lazy<std::vector<Embedding>> {
        ELOGFMT(DEBUG, "Running batch embedding task with {} contents: {}",
                contents.size(), contents);
        auto contents_size = contents.size();
        std::vector<Embedding> res;

        const size_t MAX_BATCH_SIZE = 20;
        if (contents_size > MAX_BATCH_SIZE) {
          for (size_t i = 0; i < contents_size; i += MAX_BATCH_SIZE) {
            size_t batch_size = std::min(MAX_BATCH_SIZE, contents_size - i);
            std::vector<Content> batch_contents(
                contents.begin() + i, contents.begin() + i + batch_size);

            auto batch_res =
                co_await multimodal_embedding_batch(std::move(batch_contents));
            res.insert(res.end(), batch_res.begin(), batch_res.end());

            if (i + MAX_BATCH_SIZE < contents_size) {
              ELOGFMT(DEBUG, "Waiting 1 second before processing next batch");
              co_await async_simple::coro::sleep(std::chrono::seconds(1));
            }
          }
        } else {
          res = co_await multimodal_embedding_batch(std::move(contents));
        }

        if (res.size() != contents_size) {
          auto msg = std::format(
              "Batch failed or partially failed, expected {} results, got {}",
              contents_size, res.size());
          ELOGFMT(ERROR, "{}", msg);
          co_return std::vector<Embedding>(contents_size, Embedding{});
        }

        co_return res;
      }};

  async_simple::coro::Lazy<std::vector<Embedding>>
  multimodal_embedding_batch(std::vector<Content> contents);

  async_simple::coro::Lazy<std::expected<Embedding, std::string>>
  multimodal_embedding(Content contents) override {
    if (!contents.image_path.empty()) {
      // we do not batch image embedding
      auto text_content = Content{.text = contents.text};

      auto image_content = Content{.image_path = contents.image_path};

      auto res = text_content.text.empty()
                     ? co_await multimodal_embedding_batch(
                           std::vector<Content>{image_content})
                     : co_await multimodal_embedding_batch(
                           std::vector<Content>{image_content, text_content});

      if (res.empty()) {
        ELOGFMT(ERROR, "Failed to generate embedding for content: {}",
                contents);
        co_return std::unexpected<std::string>(
            "Failed to generate embedding for content");
      }

      auto embedding = Embedding{};

      embedding[EmbeddingType::Image] = res[0][EmbeddingType::Image];
      if (res.size() == 2 && !res[1].empty()) {
        embedding[EmbeddingType::Text] = res[1][EmbeddingType::Text];
      }
      co_return embedding;
    } else if (!contents.text.empty()) {
      auto res = co_await task_pool.add_task(std::move(contents));
      if (res.empty()) {
        ELOGFMT(ERROR, "Failed to generate embedding for content: {}",
                contents);
        co_return std::unexpected<std::string>(
            "Failed to generate embedding for content");
      }
      co_return res;
    } else {
      ELOGFMT(ERROR, "No content provided for embedding");
      co_return std::unexpected<std::string>(
          "No content provided for embedding");
    }
  }

  bool support_aligned_image() const override;

  std::string api_key_;
  std::string model_id_;
  std::string endpoint_;
};

} // namespace tgdb