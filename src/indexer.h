#pragma once
#include "bot.h"
#include "td/telegram/td_api.h"
#include "td/tl/TlObject.h"
#include "ocr.h"

#include "ylt/coro_http/coro_http_client.hpp"
#include <cstdint>
#include <memory>

namespace tgdb {
struct context;
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

private:
  async_simple::coro::Lazy<void>
  index_message_batch(td::tl_object_ptr<td_api::message> message, int64_t id,
                      int64_t chat_id, std::atomic_int64_t &completed_count,
                      int total_count);
};
} // namespace tgdb