#include "indexer.h"
#include "context.h"
#include "data.h"
#include "td/telegram/td_api.h"
#include "utils.h"

#include "ylt/easylog.hpp"

#include "ylt/coro_http/coro_http_client.hpp"
#include <expected>
#include <regex>
#include <string_view>

namespace tgdb {
template <typename T> using Lazy = async_simple::coro::Lazy<T>;
Lazy<void> indexer::index_message(td::tl_object_ptr<td_api::message> message,
                                  int64_t id) {
  if (id == -1) {
    if (!message) {
      ELOGFMT(ERROR, "Failed to index message: message is null");
      co_return;
    }

    id = message->id_;
  }

  auto message_id = id;

  if (!message) {
    ELOGFMT(INFO, "Indexing message {} as empty message", id);
    // insert to databse
    co_return;
  } else {
    ELOGFMT(INFO, "Indexing message {}s", message_id);
  }

  auto user_id = std::move(message->sender_id_);
  struct message msg{
      .message_id = message_id,
  };

  msg.chat_id = message->chat_id_;

  if (auto user = try_move_as<td_api::messageSenderUser>(user_id)) {
    msg.sender.user_id = user->user_id_;

    auto user_info =
        co_await ctx.bot.query_async<td_api::getUser>(user->user_id_);

    if (!user_info) {
        ELOGFMT(ERROR, "Failed to index message {}: failed to retrieve userinfo",
            message_id);
        co_return;
    }

    if (user_info->usernames_ &&
        user_info->usernames_->active_usernames_.size() > 0) {
      msg.sender.str_id = user_info->usernames_->active_usernames_[0];
    }

    msg.sender.nickname = user_info->first_name_ + " " + user_info->last_name_;
  } else {
    ELOGFMT(ERROR, "Failed to index message {}: Unknown user type: {}",
            message_id, user_id->get_id());
    co_return;
  }
  auto send_time = message->date_;
  msg.send_time = send_time;

  auto download_file =
      [&](auto &file) -> Lazy<std::expected<std::string, std::string>> {
    if (file->local_ && file->local_->is_downloading_completed_) {
      auto file_path = file->local_->path_;
      ELOGFMT(INFO, "File path: {}", file_path);
      co_return std::filesystem::path(file_path).string();
    } else if (file->remote_) {
      ELOGFMT(INFO, "Downloading file {} for indexing...", file->id_);
      auto downloaded = co_await ctx.bot.query_async<td_api::downloadFile>(
          file->id_, 1, 0, 0, true);
      if (downloaded) {
        ELOGFMT(INFO, "File {} downloaded successfully to {}", file->id_,
                downloaded->local_->path_);
        co_return std::filesystem::path(downloaded->local_->path_).string();
      } else {
        ELOGFMT(ERROR, "Failed to download file {}, message {} skipped",
                file->remote_->id_, message_id);
        co_return std::unexpected<std::string>("Failed to download file");
      }
    } else {
      ELOGFMT(ERROR, "Failed to index message {}: Unknown file", message_id);
      co_return std::unexpected<std::string>("Unknown file type");
    }
  };

  auto process_image = [&](std::string file) -> Lazy<void> {
    if (file.empty() || file.ends_with(".webm")) {
      msg.textifyed_contents["image"] = "";
      co_return;
    }
    ELOGFMT(INFO, "Performing OCR on file {}", file);
    if (auto ocr_res = co_await ctx.ocr_client.ocr(file)) {
      ELOGFMT(INFO, "OCR result: {}", ocr_res.value());
      msg.textifyed_contents["image"] = ocr_res.value();
    } else {
      ELOGFMT(ERROR, "Failed to perform OCR on file {}: {}", file,
              ocr_res.error());
      co_return;
    }
  };

  if (auto text = try_move_as<td_api::messageText>(message->content_)) {
    msg.textifyed_contents["text"] = text->text_->text_;
  } else if (auto photo =
                 try_move_as<td_api::messagePhoto>(message->content_)) {
    if (photo->caption_)
      msg.textifyed_contents["text"] = photo->caption_->text_;

    if (auto image = co_await download_file(photo->photo_->sizes_[0]->photo_))
      co_await process_image(image.value());
  } else if (auto video =
                 try_move_as<td_api::messageVideo>(message->content_)) {

  } else if (auto sticker =
                 try_move_as<td_api::messageSticker>(message->content_)) {
    if (auto sticker_file =
            co_await download_file(sticker->sticker_->sticker_)) {
      co_await process_image(sticker_file.value());
    }
  } else {
    ELOGFMT(ERROR, "Failed to index message {}: Unknown content type: {}",
            message_id, message->content_->get_id());
    co_return;
  }

  if (message->reply_to_) {
    if (auto replyToMsg =
            try_get_as<td_api::messageReplyToMessage>(message->reply_to_)) {
      msg.reply_to_message_id = replyToMsg->message_id_;
    } else {
      msg.reply_to_message_id = -1;
    }
  }
 
  ELOGFMT(INFO, "msg indexed: {}", msg.to_string());
  ctx.message_db.put(std::to_string(message_id), msg);
}

async_simple::coro::Lazy<void> indexer::index_messages_in_chat(
    td::tl_object_ptr<td_api::chat> chat, int64_t until_id,
    std::function<void(int, int64_t)> progress_callback) {
  auto chat_id = chat->id_;
  constexpr int batch_size = 100, batch_interval = 10;

  int64_t current = 1;
  while (current <= until_id) {
    std::vector<int64_t> message_ids;

    while (current <= until_id && message_ids.size() < batch_size) {
      if (ctx.message_db.has(std::to_string(current << 20))) {
 //  ELOGFMT(INFO, "Message {} already indexed", current << 20);
        current++;
        continue;
      } else {
        message_ids.push_back(current << 20);
        current++;
      }
    }

    ELOGFMT(INFO, "Indexing messages {}", message_ids);

    auto messages = co_await ctx.bot.try_query_async<td_api::getMessages>(
        chat_id, std::vector(message_ids));

    auto futures = std::vector<Lazy<void>>();

    if (!messages) {
      if (messages.error().contains("retry after")) {
        auto regex = std::regex(R"(retry after (\d+))");
        std::smatch match;

        if (std::regex_search(messages.error(), match, regex)) {
          auto wait_time = std::stoi(match[1].str());
          ELOGFMT(WARN, "Rate limit hit, waiting for {} seconds", wait_time + 5);
          co_await async_simple::coro::sleep(std::chrono::seconds(wait_time + 5));
          continue;
        }
      }

      ELOGFMT(ERROR, "Failed to get messages for chat {}", chat_id);
      co_return;
    }

    for (int i = 0; i < messages.value()->messages_.size(); i++) {
      auto message = std::move(messages.value()->messages_[i]);
      futures.push_back(index_message(std::move(message), message_ids[i]));
    }

    co_await async_simple::coro::collectAll(std::move(futures));

    if (progress_callback) {
      ELOGFMT(INFO, "calling progress callback");
      progress_callback(chat_id, current);
      ELOGFMT(INFO, "progress callback called");
    }

    if (current < until_id) {
      ELOGFMT(INFO,
              "Waiting for {} seconds before indexing next batch of messages",
              batch_interval);
      co_await async_simple::coro::sleep(std::chrono::seconds(batch_interval));

      ELOGFMT(INFO, "Continuing to index messages...");
    }
  }
}
async_simple::coro::Lazy<void> indexer::index_messages_in_chat(
    int64_t chat_id, int64_t until_id,
    std::function<void(int, int64_t)> progress_callback) {
  auto chat = co_await ctx.bot.query_async<td_api::getChat>(chat_id);
  co_return co_await index_messages_in_chat(std::move(chat), until_id,
                                            std::move(progress_callback));
}
} // namespace tgdb
