#pragma once
#include "async_simple/Promise.h"
#include "td/telegram/Client.h"
#include "td/telegram/td_api.h"
#include "td/telegram/td_api.hpp"
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <unordered_map>

#include "async_simple/coro/Lazy.h"
#include "ylt/easylog.hpp"

namespace tgdb {
struct context;
namespace td_api = td::td_api;

struct bot {
  std::unique_ptr<td::ClientManager> client_manager_;
  size_t client_id_;
  context &ctx;
  bot(context &ctx) : ctx(ctx) {}
  void init();
  uint64_t current_query_id_ = 1;
  uint64_t next_query_id() { return ++current_query_id_; }

  std::unordered_map<int64_t, int64_t> temp_msgid_map = {};
  using Object = td_api::object_ptr<td_api::Object>;

  std::unordered_map<uint64_t, std::function<void(Object)>> handlers_;
  void process_update(int client_id, td_api::object_ptr<td_api::Object> object);
  void send_query(td_api::object_ptr<td_api::Function> f,
                  std::function<void(Object)> handler = {});
  async_simple::coro::Lazy<Object>
  send_query_async(td_api::object_ptr<td_api::Function> f);

  template <typename T> struct extract_object_ptr_type;

  template <typename T>
  struct extract_object_ptr_type<td::td_api::object_ptr<T>> {
    using type = T;
  };

  template <typename T>
  using extract_object_ptr_type_t = typename extract_object_ptr_type<T>::type;

  template <typename T,
            typename U = extract_object_ptr_type_t<typename T::ReturnType>,
            typename... Args>
  async_simple::coro::Lazy<std::expected<td_api::object_ptr<U>, std::string>>
  try_query_async(Args &&...args) {
    auto res = co_await send_query_async(
        td_api::make_object<T>(std::forward<Args>(args)...));
    if (res->get_id() != U::ID) {
      if (res->get_id() == td_api::error::ID) {
        ELOGFMT(ERROR, "Failed to query {}: error {}", T::ID,
                static_cast<td_api::error *>(res.get())->message_);

        co_return std::unexpected<std::string>(
            static_cast<td_api::error *>(res.get())->message_);
      }

      co_return std::unexpected<std::string>(std::format(
          "Failed to query {}: failed to cast type: expected {}, got {}", T::ID, U::ID, res->get_id()));
    }
    co_return td_api::move_object_as<U>(res);
  }

  template <typename T,
            typename U = extract_object_ptr_type_t<typename T::ReturnType>,
            typename... Args>
  async_simple::coro::Lazy<td_api::object_ptr<U>> query_async(Args &&...args) {
    auto res = co_await send_query_async(
        td_api::make_object<T>(std::forward<Args>(args)...));
    if (res->get_id() != U::ID) {
      if (res->get_id() == td_api::error::ID) {
        ELOGFMT(ERROR, "Failed to query {}: error {}", T::ID,
                static_cast<td_api::error *>(res.get())->message_);
      }
      co_return nullptr;
    }
    co_return td_api::move_object_as<U>(res);
  }

  async_simple::coro::Lazy<td_api::object_ptr<td_api::formattedText>>
  parse_markdown(std::string_view text) {
    return query_async<td_api::parseTextEntities>(
        std::string(text),
        td_api::make_object<td_api::textParseModeMarkdown>());
   }

 private:
   async_simple::coro::Lazy<void> handle_upgrade_database_command(int64_t chat_id);
 };
 } // namespace tgdb