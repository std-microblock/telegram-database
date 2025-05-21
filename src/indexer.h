#pragma once
#include "bot.h"
#include "data.h"
#include "td/telegram/td_api.h"
#include "td/tl/TlObject.h"
#include "ocr.h"
#include "database/vector_db.h"

#include "ylt/coro_http/coro_http_client.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace tgdb {
struct context;
struct message;

struct VectorSearchResult {
  message msg;
  float score;
};

struct indexer {
  context &ctx;
  std::unique_ptr<IOcrClient> ocr_client_;

  indexer(context &ctx);

  async_simple::coro::Lazy<void>
  index_message(td::tl_object_ptr<td_api::message> message, int64_t id = -1,
                int64_t chat_id = -1);

  async_simple::coro::Lazy<void>
  index_messages_in_chat(td::tl_object_ptr<td_api::chat> chat, int64_t until_id,
                         std::function<void(int, int64_t)>);

  async_simple::coro::Lazy<void>
  index_messages_in_chat(int64_t chat_id, int64_t until_id,
                         std::function<void(int, int64_t)> progress_callback);
                         
  // Vector search methods
  async_simple::coro::Lazy<std::vector<VectorSearchResult>>
  vector_search(const std::string& query_text, int top_k = 10);
  
  async_simple::coro::Lazy<std::vector<VectorSearchResult>>
  vector_search_image(const std::string& image_path, int top_k = 10);
  
  async_simple::coro::Lazy<std::vector<VectorSearchResult>>
  vector_search_multimodal(const std::string& query_text, const std::string& image_path, int top_k = 10);

private:
  async_simple::coro::Lazy<void>
  index_message_batch(td::tl_object_ptr<td_api::message> message, int64_t id,
                      int64_t chat_id, std::atomic_int64_t &completed_count,
                      int total_count);
                      
  async_simple::coro::Lazy<std::vector<VectorSearchResult>>
  process_search_results(const std::vector<SearchResult>& results);
};
} // namespace tgdb