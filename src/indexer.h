#pragma once
#include "bot.h"
#include "td/telegram/td_api.h"
#include "td/tl/TlObject.h"

#include "ylt/coro_http/coro_http_client.hpp"
#include <cstdint>

namespace tgdb {
struct context;
struct indexer {
  context &ctx;
  indexer(context &ctx) : ctx(ctx) {}

  async_simple::coro::Lazy<void>
  index_message(td::tl_object_ptr<td_api::message> message, int64_t id = -1);

  async_simple::coro::Lazy<void>
  index_messages_in_chat(td::tl_object_ptr<td_api::chat> chat, int64_t until_id, std::function<void(int, int64_t)>);

  async_simple::coro::Lazy<void> index_messages_in_chat(
      int64_t chat_id, int64_t until_id, std::function<void(int, int64_t)> progress_callback);
};
} // namespace tgdb