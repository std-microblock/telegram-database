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
      std::chrono::milliseconds(550),
      [this](std::vector<Content> contents)
          -> async_simple::coro::Lazy<std::vector<Embedding>> {
        ELOGFMT(DEBUG, "Running batch embedding task with {} contents: {}",
                contents.size(), contents);
        return multimodal_embedding_batch(std::move(contents));
      }};

  async_simple::coro::Lazy<std::vector<Embedding>>
  multimodal_embedding_batch(std::vector<Content> contents);

  async_simple::coro::Lazy<std::expected<Embedding, std::string>>
  multimodal_embedding(Content contents) override {
    ELOGFMT(INFO, "Running single content embedding: {}", contents);

    // contents should be separated into text and image
    if (contents.empty()) {
      ELOGFMT(ERROR, "No contents provided for embedding");
      co_return std::unexpected<std::string>(
          "No contents provided for embedding");
    }

    std::vector<async_simple::coro::Lazy<Embedding>> embedding_tasks;
    if (!contents.text.empty()) {
      embedding_tasks.push_back(
          task_pool.add_task(Content{.text = contents.text, .image_path = ""}));
    }

    if (!contents.image_path.empty()) {
      embedding_tasks.push_back(task_pool.add_task(
          Content{.text = "", .image_path = contents.image_path}));
    }

    if (embedding_tasks.empty()) {
      ELOGFMT(ERROR, "No valid contents provided for embedding");
      co_return std::unexpected<std::string>(
          "No valid contents provided for embedding");
    }

    auto res =
        co_await async_simple::coro::collectAll(std::move(embedding_tasks));

    Embedding embedding;

    for (const auto &task : res) {
      if (task.available()) {
        const auto &emb = task.value();
        for (const auto &[type, vec] : emb) {
          embedding[type].insert(embedding[type].end(), vec.begin(), vec.end());
        }
      } else {
        ELOGFMT(ERROR, "Embedding task failed");
        co_return std::unexpected<std::string>("Embedding task failed");
      }
    }

    if (embedding.empty()) {
      ELOGFMT(ERROR, "No embeddings generated for contents");
      co_return std::unexpected<std::string>("No embeddings generated for contents");
    }

    co_return embedding;
  }

  bool support_aligned_image() const override;

  std::string api_key_;
  std::string model_id_;
  std::string endpoint_;
};

} // namespace tgdb