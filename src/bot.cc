#include "bot.h"
#include "async_simple/coro/Lazy.h"
#include "cinatra/ylt/coro_io/io_context_pool.hpp"
#include "context.h"
#include "td/telegram/td_api.h"
#include "utf8.h" // Added for UTF-8 manipulation
#include "utils.h"
#include "ylt/coro_http/coro_http_client.hpp"
#include "ylt/easylog.hpp"
#include <chrono>
#include <format>
#include <thread>

namespace tgdb {
static auto tgtext(std::string text) {
  return td_api::make_object<td_api::formattedText>(
      text, std::vector<td_api::object_ptr<td_api::textEntity>>());
}

static auto tgtext(std::wstring text) {
  return tgtext(std::filesystem::path(text).string());
}

std::string ms_to_human_readable(int64_t ms) {
  auto seconds = ms / 1000;
  auto minutes = seconds / 60;
  auto hours = minutes / 60;
  auto days = hours / 24;

  if (days > 0) {
    return std::format("{}d {}h", days, hours % 24);
  } else if (hours > 0) {
    return std::format("{}h {}m", hours, minutes % 60);
  } else if (minutes > 0) {
    return std::format("{}m {}s", minutes, seconds % 60);
  } else {
    return std::format("{}s", seconds);
  }
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
                    [this](td_api::authorizationStateReady &update) {
                      ELOGFMT(INFO, "Bot logged in successfully");
                      // Send startup message
                      if (ctx.cfg.chat_id != 0) {
                        std::thread([this]() {
                          std::this_thread::sleep_for(std::chrono::seconds(1));

                          send_query(
                              td_api::make_object<td_api::sendMessage>(
                                  -1002231451498ll, 0, nullptr, nullptr,
                                  nullptr,
                                  td_api::make_object<td_api::inputMessageText>(
                                      tgtext("tgdb 已因故重启，使用 /reindex "
                                             "来索引丢失的消息"),
                                      nullptr, false)),
                              [](auto &&obj) {
                                if (obj->get_id() == td_api::error::ID) {
                                  auto error =
                                      td_api::move_object_as<td_api::error>(
                                          obj);
                                  ELOGFMT(ERROR,
                                          "Failed to send startup message: {}",
                                          error->message_);
                                } else {
                                  ELOGFMT(INFO,
                                          "Startup message sent successfully");
                                }
                              });
                        }).detach();
                      } else {
                        ELOGFMT(WARNING, "admin_chat_id is not set, cannot "
                                         "send startup message.");
                      }
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

              ELOGFMT(INFO, "New message: {} chat: {}", content,
                      update.message_->chat_id_);
              if (content.starts_with("/reindex")) {
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
                               chat_id = msg.value()->chat_id_,
                               begin_time = std::chrono::steady_clock::now()
                                                .time_since_epoch()
                                                .count()](
                                  int64_t _, int message_id) mutable {
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
                                                "Reindex progress {}/{} ETA: "
                                                "{}",
                                                message_id, msgid >> 20,
                                                ms_to_human_readable(
                                                    (std::chrono::steady_clock::
                                                         now()
                                                             .time_since_epoch()
                                                             .count() -
                                                     begin_time) *
                                                    ((msgid >> 20) -
                                                     message_id) /
                                                    100 / 1000 / 1000))),
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

                                begin_time = std::chrono::steady_clock::now()
                                                 .time_since_epoch()
                                                 .count();
                              })
                          .start([](auto &&) {});
                    });
              }

              if (content.starts_with("/info") && update.message_->reply_to_) {
                auto reply_to = try_get_as<td_api::messageReplyToMessage>(
                    update.message_->reply_to_);

                auto mode = content.length() > 6 ? content.substr(6)
                                                 : std::string("db");

                ctx.bot
                    .query_async<td_api::getMessage>(reply_to->chat_id_,
                                                     reply_to->message_id_)
                    .start([this, mode,
                            chat_id = update.message_->chat_id_](auto &&msg) {
                      if (auto td_message = std::move(msg.value())) {
                        std::string info_text;
                        if (mode.contains("td"))
                          info_text += to_string(td_message);
                        info_text += "\n\n";
                        // Check if message is in local databaseff
                        auto db_message =
                            ctx.message_db.get(std::to_string(td_message->id_));
                        if (db_message && mode.contains("db")) {
                          info_text +=
                              "Indexed content:\n" + db_message->to_string();
                        } else {
                          info_text += "Not indexed";
                        }

                        auto imt =
                            td_api::make_object<td_api::inputMessageText>(
                                tgtext(info_text), nullptr, false);
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
              } else if (content == "/upgradedatabase") {
                handle_upgrade_database_command(update.message_->chat_id_)
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
            answer->cache_time_ = 0; // Short cache time
            answer->is_personal_ = true;

            if (update.query_.empty()) {
              // Handle empty query: send a prompt message
              auto result =
                  td_api::make_object<td_api::inputInlineQueryResultArticle>();
              result->id_ = "empty_query_prompt";
              result->title_ =
                  "请输入搜索内容"; // "Please enter search content"
              result->description_ =
                  "输入关键词以搜索消息"; // "Enter keywords to search messages"
              result->input_message_content_ =
                  td_api::make_object<td_api::inputMessageText>(
                      tgtext("请输入搜索内容"), nullptr, false);

              answer->results_.push_back(std::move(result));
              answer->next_offset_ = ""; // No more results

              send_query(std::move(answer), [this](auto obj) {
                if (obj->get_id() == td_api::error::ID) {
                  auto error = td_api::move_object_as<td_api::error>(obj);
                  ELOGFMT(ERROR, "Error sending empty inline query prompt: {}",
                          error->message_);
                } else {
                  ELOGFMT(INFO, "Empty inline query prompted successfully");
                }
              });
              return; // Exit the handler
            }
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

              std::string query_str = update.query_;
              std::string content_str;

              for (auto &[key, _value_str] : message.textifyed_contents) {

                size_t found_byte_offset_in_value = _value_str.find(query_str);

                if (found_byte_offset_in_value != std::string::npos) {
                  if (content_str.size() > 0)
                    content_str += "\n";
                  size_t value_cp_total =
                      utf8::distance(_value_str.begin(), _value_str.end());
                  size_t query_cp_len =
                      utf8::distance(query_str.begin(), query_str.end());

                  int padding_cp_each_side = 0;
                  if (70 > (int)query_cp_len) { // Aim for ~70 codepoints total
                                                // for query + padding
                    padding_cp_each_side = (70 - (int)query_cp_len) / 2;
                  }
                  padding_cp_each_side =
                      std::min(30, padding_cp_each_side); // Cap padding
                  padding_cp_each_side =
                      std::max(0, padding_cp_each_side); // Ensure non-negative

                  std::string snippet_to_add;
                  // Condition to truncate: if value is larger than ideal
                  // snippet or generally over ~60 codepoints
                  if (value_cp_total > (query_cp_len +
                                        2 * (size_t)padding_cp_each_side + 5) ||
                      value_cp_total > 60) {
                    auto match_start_byte_it =
                        _value_str.begin() + found_byte_offset_in_value;
                    size_t match_start_cp_offset =
                        utf8::distance(_value_str.begin(), match_start_byte_it);

                    size_t snippet_start_cp =
                        (match_start_cp_offset > (size_t)padding_cp_each_side)
                            ? (match_start_cp_offset - padding_cp_each_side)
                            : 0;

                    size_t snippet_end_cp = std::min(
                        value_cp_total, match_start_cp_offset + query_cp_len +
                                            (size_t)padding_cp_each_side);

                    auto snippet_start_byte_it = _value_str.begin();
                    utf8::advance(snippet_start_byte_it, snippet_start_cp,
                                  _value_str.end());

                    auto snippet_end_byte_it = _value_str.begin();
                    utf8::advance(snippet_end_byte_it, snippet_end_cp,
                                  _value_str.end());

                    snippet_to_add =
                        std::string(snippet_start_byte_it, snippet_end_byte_it);

                    bool add_prefix = snippet_start_cp > 0;
                    bool add_suffix = snippet_end_cp < value_cp_total;

                    if (add_prefix)
                      snippet_to_add = "..." + snippet_to_add;
                    if (add_suffix)
                      snippet_to_add = snippet_to_add + "...";
                  } else {
                    snippet_to_add = _value_str;
                  }
                  content_str += snippet_to_add;
                }
              }

              std::string final_description_str;
              if (content_str.empty()) {
                final_description_str = "empty";
              } else {
                size_t content_cp_len =
                    utf8::distance(content_str.begin(), content_str.end());
                if (content_cp_len > 200) {
                  auto desc_end_it = content_str.begin();
                  utf8::advance(desc_end_it, 200, content_str.end());
                  final_description_str =
                      std::string(content_str.begin(), desc_end_it) + "...";
                } else {
                  final_description_str = content_str;
                }
              }
              result->description_ =
                  final_description_str; // Use the processed std::string

              auto text_content = td_api::make_object<td_api::inputMessageText>(
                  tgtext(content_str), nullptr, false);

              text_content->text_->entities_ =
                  std::vector<td_api::object_ptr<td_api::textEntity>>{};

              size_t current_search_pos_bytes = 0;
              while (true) {
                size_t found_byte_offset =
                    content_str.find(query_str, current_search_pos_bytes);
                if (found_byte_offset == std::string::npos) {
                  break;
                }
                auto text_entity = td_api::make_object<td_api::textEntity>();
                text_entity->offset_ =
                    utf8::distance(content_str.begin(),
                                   content_str.begin() + found_byte_offset);
                text_entity->length_ =
                    utf8::distance(query_str.begin(), query_str.end());
                text_entity->type_ =
                    td_api::make_object<td_api::textEntityTypeBold>();
                text_content->text_->entities_.push_back(
                    std::move(text_entity));
                current_search_pos_bytes =
                    found_byte_offset + query_str.length();
              }

              result->input_message_content_ = std::move(text_content);

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

async_simple::coro::Lazy<void>
tgdb::bot::handle_upgrade_database_command(int64_t chat_id) {
  co_await query_async<td_api::sendMessage>(
      chat_id, 0, nullptr, nullptr, nullptr,
      td_api::make_object<td_api::inputMessageText>(
          tgtext("Starting database upgrade..."), nullptr, false));

  int upgraded_count = 0;
  for (auto &[key, message] : ctx.message_db) {
    if (!message.image_file.has_value() ||
        !message.image_file.value().has_value() ||
        message.image_file->value().empty()) {
      ELOGFMT(INFO, "Message {} in chat {} is missing image_file, fetching...",
              message.message_id >> 20, message.chat_id);
      auto fetched_message = co_await query_async<td_api::getMessage>(
          message.chat_id, message.message_id);
      if (fetched_message) {
        if (auto msg_content =
                try_get_as<td_api::messagePhoto>(fetched_message->content_)) {
          if (msg_content->photo_ && msg_content->photo_->sizes_.size() > 0) {
            // Get the largest photo size
            auto &largest_size = msg_content->photo_->sizes_.back();
            if (largest_size->photo_ && largest_size->photo_->local_.get()) {
              message.image_file = largest_size->photo_->local_->path_;
              ctx.message_db.put(key, message);
              upgraded_count++;
              ELOGFMT(INFO, "Upgraded message {} with image file: {}",
                      message.message_id, message.image_file->value());
              continue;
            }
          }
        } else if (auto msg_content = try_get_as<td_api::messageSticker>(
                       fetched_message->content_)) {
          if (msg_content->sticker_ &&
              msg_content->sticker_->sticker_->local_.get()) {
            message.image_file = msg_content->sticker_->sticker_->local_->path_;
            ctx.message_db.put(key, message);
            upgraded_count++;
            ELOGFMT(INFO, "Upgraded message {} with document file: {}",
                    message.message_id, message.image_file->value());
            continue;
          }
        } else {
          ELOGFMT(INFO, "Message {} in chat {} is not a photo or sticker",
                  message.message_id, message.chat_id);
          message.image_file = std::nullopt;
          ctx.message_db.put(key, message);
          continue;
        }

        ELOGFMT(WARNING,
                "Message {} in chat {} has a image but do not have local file",
                message.message_id, message.chat_id);
      } else {
        ELOGFMT(ERROR, "Failed to fetch message {} in chat {}",
                message.message_id, message.chat_id);
      }
    }
  }

  co_await query_async<td_api::sendMessage>(
      chat_id, 0, nullptr, nullptr, nullptr,
      td_api::make_object<td_api::inputMessageText>(
          tgtext(std::format("Database upgrade complete. {} messages upgraded.",
                             upgraded_count)),
          nullptr, false));
}