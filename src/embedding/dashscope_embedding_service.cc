#include "dashscope_embedding_service.h"
#include "cinatra/define.h"
#include "cinatra/utils.hpp"
#include "rfl/json/read.hpp"
#include "rfl/json/write.hpp"
#include "ylt/coro_http/coro_http_client.hpp"
#include "ylt/easylog.hpp"

namespace tgdb {

async_simple::coro::Lazy<std::vector<Embedding>>
tgdb::DashScopeEmbeddingService::multimodal_embedding_batch(
    std::vector<Content> contents) {
  if (contents.empty()) {
    ELOGFMT(ERROR, "No contents provided for embedding");
    co_return std::vector<Embedding>{};
  } else {
    ELOGFMT(INFO, "Batch embedding with {} contents", contents.size());
  }

  DashScopeEmbeddingRequest request;
  request.model = model_id_;

  for (const auto &content : contents) {
    std::map<std::string, std::string> req_content;

    if (!content.text.empty()) {
      req_content["text"] = content.text;
    }

    if (!content.image_path.empty()) {
      if (content.image_path.find("http") == std::string::npos) {
        req_content["image"] = "data:image/";
        auto ext =
            content.image_path.substr(content.image_path.find_last_of('.') + 1);
        if (ext == "jpg" || ext == "jpeg") {
          req_content["image"] += "jpeg;base64,";
        } else if (ext == "png") {
          req_content["image"] += "png;base64,";
        } else if (ext == "webp") {
          req_content["image"] += "webp;base64,";
        } else {
          ELOGFMT(ERROR, "Unsupported image format: {}", ext);
          co_return std::vector<Embedding>{};
        }

        std::ifstream file(content.image_path, std::ios::binary);
        if (!file) {
          ELOGFMT(ERROR, "Failed to open image file: {}", content.image_path);
          co_return std::vector<Embedding>{};
        }

        std::string image_data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());

        std::string base64_image = cinatra::base64_encode(image_data);
        req_content["image"] += base64_image;
        file.close();
      } else {
        req_content["image"] = content.image_path;
      }
    }

    if (!content.video_path.empty()) {
      if (content.video_path.find("http") == std::string::npos) {
        ELOGFMT(ERROR, "Unsupported video format: {}", content.video_path);
        co_return std::vector<Embedding>{};
      } else {
        req_content["video"] = content.video_path;
      }
    }

    request.input.contents.push_back(req_content);
  }

  int retry_count = 0;
retry:
  auto json_request = rfl::json::write(request);
  coro_http::coro_http_client client;
  client.set_conn_timeout(std::chrono::seconds(10));
  client.set_req_timeout(std::chrono::seconds(50));

  client.set_proxy("localhost", "9000");

  auto result = co_await client.async_post(
      endpoint_, json_request, cinatra::req_content_type::json,
      {{"Authorization", "Bearer " + api_key_}});

  if (result.net_err) {
    ELOGFMT(ERROR, "DashScope API error: {}", result.net_err.message());
    if (retry_count < 3) {
      retry_count++;
      ELOGFMT(INFO, "Retrying DashScope API request, attempt {}", retry_count);
      goto retry;
    }

    co_return std::vector<Embedding>{};
  }

  if (result.status != 200) {
    ELOGFMT(ERROR, "DashScope API error: {} {}", result.status,
            result.resp_body);
    if (retry_count < 3) {
      retry_count++;
      ELOGFMT(INFO, "Retrying DashScope API request, attempt {}", retry_count);
      goto retry;
    }

    co_return std::vector<Embedding>{};
  }

  auto response_result =
      rfl::json::read<DashScopeEmbeddingResponse>(result.resp_body);
  if (!response_result) {
    ELOGFMT(ERROR, "Failed to parse DashScope response: {} {}",
            response_result.error().what(), result.resp_body);
    co_return std::vector<Embedding>{};
  }

  auto response = response_result.value();

  std::vector<Embedding> embeddings;
  for (const auto &emb : response.output.embeddings) {
    Embedding embedding;
    if (emb.type == "text") {
      embedding[EmbeddingType::Text] = emb.embedding;
    } else if (emb.type == "image") {
      embedding[EmbeddingType::Image] = emb.embedding;
    } else {
      ELOGFMT(ERROR, "Unsupported embedding type: {}", emb.type);
      co_return std::vector<Embedding>{};
    }
    embeddings.push_back(embedding);
  }

  co_return embeddings;
}
std::string tgdb::DashScopeEmbeddingService::get_id() const {
  return "dashscope-" + model_id_;
}
bool tgdb::DashScopeEmbeddingService::support_aligned_image() const {
  return true;
}
tgdb::DashScopeEmbeddingService::DashScopeEmbeddingService(
    const std::string &api_key, const std::string &model_id)
    : api_key_(api_key), model_id_(model_id),
      endpoint_("https://dashscope.aliyuncs.com/api/v1/services/embeddings/"
                "multimodal-embedding/multimodal-embedding") {}

} // namespace tgdb