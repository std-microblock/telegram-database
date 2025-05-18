#include "bot.h"
#include "async_simple/coro/Lazy.h"
#include "cinatra/ylt/coro_io/io_context_pool.hpp"
#include "context.h"
#include "td/telegram/td_api.h"
#include "utils.h"
#include "ylt/coro_http/coro_http_client.hpp"
#include "ylt/easylog.hpp"
#include <format>
#include <thread>

namespace tgdb {
static auto tgtext(std::string text) {
  return td_api::make_object<td_api::formattedText>(
      text, std::vector<td_api::object_ptr<td_api::textEntity>>());
}

void bot::process_update(int client_id,
                         td_api::object_ptr<td_api::Object> object) {
  td_api::downcast_call(
      *object,
      overloaded(
          // authentication part
          [this](td_api::updateAuthorizationState &update_authorization_state) {
            auto authorization_state_ =
                std::move(update_authorization_state.authorization_state_);
            td_api::downcast_call(
                *authorization_state_,
                overloaded(
                    [this](td_api::authorizationStateWaitPhoneNumber &update) {
                      ELOGFMT(INFO, "Logging in with bot token...");
                      send_query(td_api::make_object<
                                 td_api::checkAuthenticationBotToken>(
                          ctx.cfg.bot_token));
                    },
                    [](td_api::authorizationStateReady &update) {
                      ELOGFMT(INFO, "Bot logged in successfully");
                    },
                    [this](
                        td_api::authorizationStateWaitTdlibParameters &update) {
                      ELOGFMT(INFO, "Authorization state: WaitTdlibParameters");
                      auto request =
                          td_api::make_object<td_api::setTdlibParameters>();
                      request->database_directory_ = "tdlib";
                      request->api_id_ = ctx.cfg.api_id;
                      request->api_hash_ = ctx.cfg.api_hash;
                      request->system_language_code_ = "en";
                      request->device_model_ = ctx.cfg.device_model;
                      request->application_version_ = "1.0";
                      send_query(std::move(request), [this](auto obj) {
                        if (obj->get_id() == td_api::error::ID) {
                          auto error =
                              td_api::move_object_as<td_api::error>(obj);
                          ELOGFMT(ERROR, "Error: {}", error->message_);
                        } else {
                          ELOGFMT(INFO, "Tdlib parameters set successfully");
                        }
                      });
                    }));
          },
          // message part
          [this](td_api::updateNewMessage &update) {
            if (auto msg = try_get_as<td_api::messageText>(
                    update.message_->content_)) {
              auto content = msg->text_->text_;

              ELOGFMT(INFO, "New message: {}", content);
              if (content == "/reindex") {
                query_async<td_api::sendMessage>(
                    update.message_->chat_id_, 0, nullptr, nullptr, nullptr,
                    td_api::make_object<td_api::inputMessageText>(
                        tgtext("Reindexing messages..."), nullptr, false))
                    .start([chat_id = update.message_->chat_id_,
                            msgid = update.message_->id_, this](auto &&msg) {
                      ELOGFMT(INFO, "Reindexing messages... tips msgid {}",
                              msg.value()->id_);
                      ctx.indexer
                          .index_messages_in_chat(
                              chat_id, msgid,
                              [msgid2 = msg.value()->id_, this, msgid,
                               chat_id = msg.value()->chat_id_](
                                  int64_t _, int message_id) {
                                ELOGFMT(INFO, "update reindex progress: {} {}",
                                        message_id, msgid2);
                                ctx.bot.send_query(
                                    td_api::make_object<
                                        td_api::editMessageText>(
                                        chat_id, temp_msgid_map[msgid2],
                                        nullptr,
                                        td_api::make_object<
                                            td_api::inputMessageText>(
                                            tgtext(std::format(
                                                "Reindex progress {}/{}",
                                                message_id, msgid >> 20)),
                                            nullptr, false)),
                                    [](auto &&res) {
                                      if (res->get_id() == td_api::error::ID) {
                                        auto error = td_api::move_object_as<
                                            td_api::error>(res);
                                        ELOGFMT(ERROR, "Error: {}",
                                                error->message_);
                                      } else {
                                        ELOGFMT(INFO,
                                                "Reindex progress updated");
                                      }
                                    });
                              })
                          .start([](auto &&) {});
                    });
              }

              if (content == "/info" && update.message_->reply_to_) {
                auto reply_to = try_get_as<td_api::messageReplyToMessage>(
                    update.message_->reply_to_);

                ctx.bot
                    .query_async<td_api::getMessage>(reply_to->chat_id_,
                                                     reply_to->message_id_)
                    .start([this,
                            chat_id = update.message_->chat_id_](auto &&msg) {
                      if (auto message = std::move(msg.value())) {
                        auto imt =
                            td_api::make_object<td_api::inputMessageText>(
                                tgtext(to_string(message)), nullptr, false);
                        ctx.bot
                            .query_async<td_api::sendMessage>(
                                chat_id, 0, nullptr, nullptr, nullptr,
                                std::move(imt))
                            .start([](auto &&) {});
                      } else {
                        ELOGFMT(ERROR, "Failed to get message info");
                      }
                    });
              }

              if (content == "/ping") {
                query_async<td_api::sendMessage>(
                    update.message_->chat_id_, 0, nullptr, nullptr, nullptr,
                    td_api::make_object<td_api::inputMessageText>(
                        tgtext("Pong!"), nullptr, false))
                    .start([](auto &&) {});
              }
            }

            ctx.indexer.index_message(std::move(update.message_))
                .start([](auto &&) {});
          },
          // inline query
          [this](td_api::updateNewInlineQuery &update) {
            ELOGFMT(INFO, "New inline query: {}", update.query_);
            auto answer = td_api::make_object<td_api::answerInlineQuery>();
            answer->inline_query_id_ = update.id_;
            answer->results_ = std::vector<
                td_api::object_ptr<td_api::InputInlineQueryResult>>{};

            answer->cache_time_ = 0;
            answer->is_personal_ = true;

            auto offset =
                update.offset_.size() ? std::stoull(update.offset_) : 0;

            int counter = 0;
            for (auto &[msgid, message] : ctx.message_db) {
              counter++;

              if (counter < offset) {
                continue;
              }

              if (message.textifyed_contents.size() == 0 ||
                  std::ranges::none_of(
                      message.textifyed_contents, [&](auto &pair) {
                        return pair.second.contains(update.query_);
                      })) {
                continue;
              }

              auto result =
                  td_api::make_object<td_api::inputInlineQueryResultArticle>();
              result->id_ = std::to_string(message.message_id);
              result->title_ = message.sender.nickname;
              result->description_ =
                  message.textifyed_contents.size() > 0
                      ? message.textifyed_contents.begin()->second
                      : "No content";

              std::string content;
              for (auto &[key, value] : message.textifyed_contents) {
                content += key + ": " + value + "\n";
              }

              result->input_message_content_ =
                  td_api::make_object<td_api::inputMessageText>(tgtext(content),
                                                                nullptr, false);

              auto kbd = std::vector<std::vector<
                  td_api::object_ptr<td_api::inlineKeyboardButton>>>{};

              kbd.emplace_back();

              kbd[0].push_back(
                  td_api::make_object<td_api::inlineKeyboardButton>(
                      "原消息",
                      td_api::make_object<td_api::inlineKeyboardButtonTypeUrl>(
                          std::format("https://t.me/c/{}/{}",
                                      -(message.chat_id + 1e12),
                                      message.message_id >> 20))));

              kbd[0].push_back(
                  td_api::make_object<td_api::inlineKeyboardButton>(
                      "全部结果",
                      td_api::make_object<
                          td_api::inlineKeyboardButtonTypeSwitchInline>(
                          update.query_,
                          td_api::make_object<td_api::targetChatCurrent>())));

              result->reply_markup_ =
                  td_api::make_object<td_api::replyMarkupInlineKeyboard>(
                      std::move(kbd));

              answer->results_.push_back(std::move(result));

              if (answer->results_.size() > 10) {
                break;
              }
            }

            answer->next_offset_ = std::to_string(counter + 1);

            send_query(std::move(answer), [this,
                                           size = answer->results_.size()](
                                              auto obj) {
              if (obj->get_id() == td_api::error::ID) {
                auto error = td_api::move_object_as<td_api::error>(obj);
                ELOGFMT(ERROR, "Error: {}", error->message_);
              } else {
                ELOGFMT(INFO, "Inline query answered successfully, size: {}",
                        size);
              }
            });
          },
          // msg sent successfully
          [this](td_api::updateMessageSendSucceeded &update) {
            temp_msgid_map[update.old_message_id_] = update.message_->id_;
          }));
}
void bot::init() {
  td::ClientManager::execute(
      td_api::make_object<td_api::setLogVerbosityLevel>(1));
  client_manager_ = std::make_unique<td::ClientManager>();
  client_id_ = client_manager_->create_client_id();

  ELOGFMT(INFO, "Telegram Client initialized, ID: {}", client_id_);
  send_query(td_api::make_object<td_api::getOption>("version"), {});

  std::thread([this]() {
    while (true) {
      auto response = client_manager_->receive(10);
      if (response.object == nullptr) {
        continue;
      }

      if (response.request_id == 0) {
        process_update(response.client_id, std::move(response.object));
      }
      auto it = handlers_.find(response.request_id);
      if (it != handlers_.end()) {
        it->second(std::move(response.object));
        handlers_.erase(it);
      }
    }
  }).detach();
}
void bot::send_query(td_api::object_ptr<td_api::Function> f,
                     std::function<void(Object)> handler) {
  auto query_id = next_query_id();
  if (handler) {
    handlers_.emplace(query_id, std::move(handler));
  }
  client_manager_->send(client_id_, query_id, std::move(f));
}

async_simple::coro::Lazy<bot::Object>
bot::send_query_async(td_api::object_ptr<td_api::Function> f) {
  auto promise = std::make_shared<async_simple::Promise<Object>>();
  auto future = promise->getFuture().via(coro_io::get_global_executor());

  send_query(std::move(f),
             [promise](Object obj) { promise->setValue(std::move(obj)); });

  Object result = co_await std::move(future);
  co_return result;
}
}; // namespace tgdb