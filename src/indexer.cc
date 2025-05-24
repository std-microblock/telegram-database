#include "indexer.h"
#include "context.h"
#include "data.h"
#include "embedding/embedding_service.h"
#include "td/telegram/td_api.h"
#include "utils.h"

#include "ylt/easylog.hpp"

#include "ylt/coro_http/coro_http_client.hpp"
#include <atomic>
#include <expected>
#include <ranges>
#include <regex>
#include <string_view>

namespace tgdb {
template <typename T> using Lazy = async_simple::coro::Lazy<T>;

indexer::indexer(context &ctx) : ctx(ctx) {
  if (ctx.cfg.ocr_config && !ctx.cfg.ocr_config->api_url.empty()) {
    ocr_client_ = std::make_unique<OcrClient>(ctx.cfg.ocr_config->api_url);
  } else {
    ocr_client_ = std::make_unique<NullOcrClient>();
  }
}

Lazy<void> indexer::index_message(td::tl_object_ptr<td_api::message> message,
                                  int64_t id, int64_t chat_id) {
  if (id == -1) {
    if (!message) {
      ELOGFMT(ERROR, "Failed to index message: message is null");
      co_return;
    }

    id = message->id_;
  }

  if (chat_id == -1) {
    if (message)
      chat_id = message->chat_id_;
    else {
      ELOGFMT(ERROR, "Failed to index message: chat_id is null");
      co_return;
    }
  }

  if (!message) {
    ELOGFMT(INFO, "Indexing message {} as empty message", id);
    ctx.message_db.put(std::to_string(id), tgdb::message{
                                               .message_id = id,
                                               .chat_id = chat_id,
                                               .textifyed_contents = {},
                                           });
    co_return;
  } else {
    ELOGFMT(INFO, "Indexing message {}", id);
  }

  auto user_id = std::move(message->sender_id_);
  struct message msg{
      .message_id = id,
  };

  msg.chat_id = message->chat_id_;

  if (auto user = try_move_as<td_api::messageSenderUser>(user_id)) {
    msg.sender.user_id = user->user_id_;

    auto user_info =
        co_await ctx.bot.query_async<td_api::getUser>(user->user_id_);

    if (!user_info) {
      ELOGFMT(ERROR, "Failed to index message {}: failed to retrieve userinfo",
              id);
      co_return;
    }

    if (user_info->usernames_ &&
        user_info->usernames_->active_usernames_.size() > 0) {
      msg.sender.str_id = user_info->usernames_->active_usernames_[0];
    }

    msg.sender.nickname = user_info->first_name_ + " " + user_info->last_name_;
  } else {
    ELOGFMT(ERROR, "Failed to index message {}: Unknown user type: {}", id,
            user_id->get_id());
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
                file->remote_->id_, id);
        co_return std::unexpected<std::string>("Failed to download file");
      }
    } else {
      ELOGFMT(ERROR, "Failed to index message {}: Unknown file", id);
      co_return std::unexpected<std::string>("Unknown file type");
    }
  };

  auto process_image = [&](std::string file) -> Lazy<void> {
    if (file.empty() || file.ends_with(".webm")) {
      msg.textifyed_contents["image"] = "";
      co_return;
    }

    msg.image_file = file;
    if (ocr_client_) {
      ELOGFMT(INFO, "Performing OCR on file {}", file);
      if (auto ocr_res = co_await ocr_client_->ocr(file)) {
        ELOGFMT(INFO, "OCR result: {}", ocr_res.value());
        msg.textifyed_contents["image"] = ocr_res.value();
      } else {
        ELOGFMT(ERROR, "Failed to perform OCR on file {}: {}", file,
                ocr_res.error());
        co_return;
      }
    } else {
      ELOGFMT(INFO, "OCR client not initialized, skipping OCR for file {}",
              file);
    }
  };

  constexpr static auto contentTypesFunctionalMessages = {
      td_api::messageChatJoinByRequest::ID,
      td_api::messageChatDeleteMember::ID,
      td_api::messageChatUpgradeTo::ID,
      td_api::messageChatUpgradeFrom::ID,
      td_api::messagePinMessage::ID,
      td_api::messageChatSetMessageAutoDeleteTime::ID,
      td_api::messageChatSetTheme::ID,
      td_api::messageChatJoinByLink::ID,
      td_api::messageChatSetBackground::ID};

  if (auto text = try_move_as<td_api::messageText>(message->content_)) {
    msg.textifyed_contents["text"] = text->text_->text_;
  } else if (auto photo =
                 try_move_as<td_api::messagePhoto>(message->content_)) {
    if (photo->caption_)
      msg.textifyed_contents["text"] = photo->caption_->text_;

    if (auto image =
            co_await download_file(photo->photo_->sizes_.back()->photo_))
      co_await process_image(image.value());
  } else if (auto video =
                 try_move_as<td_api::messageVideo>(message->content_)) {

  } else if (auto sticker =
                 try_move_as<td_api::messageSticker>(message->content_)) {
    if (auto sticker_file =
            co_await download_file(sticker->sticker_->sticker_)) {
      co_await process_image(sticker_file.value());
    }
  } else if (auto document =
                 try_move_as<td_api::messageDocument>(message->content_)) {
    if (document->caption_)
      msg.textifyed_contents["text"] = document->caption_->text_;

    if (auto doc_file =
            co_await download_file(document->document_->document_)) {
      msg.textifyed_contents["document"] = doc_file.value();
    }
  } else if (auto audio =
                 try_move_as<td_api::messageAudio>(message->content_)) {
    if (audio->caption_)
      msg.textifyed_contents["text"] = audio->caption_->text_;
    if (auto audio_file = co_await download_file(audio->audio_->audio_)) {
      msg.textifyed_contents["audio"] = audio_file.value();
    }
  } else if (auto voice =
                 try_move_as<td_api::messageVoiceNote>(message->content_)) {
    if (voice->caption_)
      msg.textifyed_contents["text"] = voice->caption_->text_;
    msg.textifyed_contents["voice"] = std::format(
        "MIME type: {}, duration: {}s", voice->voice_note_->mime_type_,
        voice->voice_note_->duration_ / 1000);
  } else if (auto video_note =
                 try_move_as<td_api::messageVideoNote>(message->content_)) {
    if (video_note->video_note_->video_) {
      if (auto video_note_file =
              co_await download_file(video_note->video_note_->video_)) {
        co_await process_image(video_note_file.value());
      }
    }
  } else if (auto location =
                 try_move_as<td_api::messageLocation>(message->content_)) {
    msg.textifyed_contents["location"] = std::format(
        "Latitude: {}, Longitude: {}", location->location_->latitude_,
        location->location_->longitude_);
  } else if (auto contact =
                 try_move_as<td_api::messageContact>(message->content_)) {
    msg.textifyed_contents["contact"] =
        std::format("Name: {}, Phone: {}", contact->contact_->first_name_,
                    contact->contact_->phone_number_);
  } else if (auto venue =
                 try_move_as<td_api::messageVenue>(message->content_)) {
    msg.textifyed_contents["venue"] =
        std::format("Title: {}, Address: {}", venue->venue_->title_,
                    venue->venue_->address_);
  } else if (std::ranges::contains(contentTypesFunctionalMessages,
                                   message->content_->get_id())) {
    auto str = to_string(message->content_);
    msg.textifyed_contents["functional_message"] = str.substr(0, str.find(' '));
  } else {
    ELOGFMT(ERROR, "Failed to index message {}: Unknown content type: {}", id,
            message->content_->get_id());
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

  ELOGFMT(INFO, "map1: {}", msg.textifyed_contents.empty());
  ELOGFMT(INFO, "msg indexed: {}", msg.to_string());
  ctx.message_db.put(std::to_string(id), msg);

  if (ctx.embedding_service_ && ctx.vector_db_service_) {
    ELOGFMT(INFO, "Generating embeddings for message {}", id);

    Content content;

    std::string combined_text;
    for (const auto &[type, text] : msg.textifyed_contents) {
      if (!text.empty()) {
        combined_text += text + " ";
      }
    }

    if (!combined_text.empty()) {
      content.text = combined_text;
    }

    if (msg.image_file.has_value() && msg.image_file.value().has_value()) {

      if (ctx.embedding_service_->support_aligned_image()) {
        content.image_path = msg.image_file.value().value();
      }
    }

    if (!content.empty()) {
      try {
        auto embedding = co_await ctx.embedding_service_->multimodal_embedding(
            std::move(content));

        if (embedding && !embedding->empty()) {
          ELOGFMT(INFO, "Generated embedding for message {}", id);

          const std::string key = std::to_string(id);

          for (const auto &[type, vec] : embedding.value())
            if (ctx.vector_db_service_->AddVector(
                    key + std::format(":type-{}", (int)type), vec)) {
              ELOGFMT(
                  INFO,
                  "Added vector embedding for message {} to vector database",
                  id);
            } else {
              ELOGFMT(ERROR,
                      "Failed to add vector embedding for message {} to vector "
                      "database",
                      id);
            }
        } else {
          ELOGFMT(WARNING, "No embeddings generated for message {}", id);
        }
      } catch (const std::exception &e) {
        ELOGFMT(ERROR, "Error generating embeddings for message {}: {}", id,
                e.what());
      }
    } else {
      ELOGFMT(INFO, "No content available for embedding in message {}", id);
    }
  }
}

Lazy<void> indexer::index_message_batch(
    td::tl_object_ptr<td_api::message> message, int64_t id, int64_t chat_id,
    std::atomic_int64_t &completed_count, int total_count) {
  co_await index_message(std::move(message), id, chat_id);
  auto count = completed_count.fetch_add(1) + 1;
  ELOGFMT(INFO, "Batch progress: {}/{}", count, total_count);
}

async_simple::coro::Lazy<void> indexer::index_messages_in_chat(
    td::tl_object_ptr<td_api::chat> chat, int64_t until_id,
    std::function<void(int, int64_t)> progress_callback) {
  auto chat_id = chat->id_;
  constexpr int batch_size = 100, batch_interval = 10;

  int64_t current = 1;
  while (current <= until_id) {
    std::vector<int64_t> message_ids;

    ELOGFMT(INFO, "Acquiring missing message ids for chat {}", chat_id);
    while (current <= until_id && message_ids.size() < batch_size) {
      if (current % 1000 == 0) {
        ELOGFMT(INFO, "ids for chat {}: {}", chat_id, current);
      }
      if (ctx.message_db.has(std::to_string(current << 20))) {
        current++;
        continue;
      } else {
        message_ids.push_back(current << 20);
        current++;
      }
    }

    ELOGFMT(INFO, "Indexing messages {}",
            message_ids | std::ranges::views::transform(
                              [](auto id) { return id >> 20; }));

    auto messages = co_await ctx.bot.try_query_async<td_api::getMessages>(
        chat_id, std::vector(message_ids));

    auto futures = std::vector<Lazy<void>>();

    if (!messages) {
      if (messages.error().contains("retry after")) {
        auto regex = std::regex(R"(retry after (\d+))");
        std::smatch match;

        if (std::regex_search(messages.error(), match, regex)) {
          auto wait_time = std::stoi(match[1].str());
          ELOGFMT(WARN, "Rate limit hit, waiting for {} seconds",
                  wait_time + 5);
          co_await async_simple::coro::sleep(
              std::chrono::seconds(wait_time + 5));
          continue;
        }
      }

      ELOGFMT(ERROR, "Failed to get messages for chat {}", chat_id);
      co_return;
    }

    std::atomic_int64_t completed_count = 0;
    for (int i = 0; i < messages.value()->messages_.size(); i++) {
      auto message = std::move(messages.value()->messages_[i]);

      futures.push_back(index_message_batch(std::move(message), message_ids[i],
                                            chat_id, completed_count,
                                            message_ids.size()));
    }

    co_await async_simple::coro::collectAllWindowedPara(10, false,
                                                        std::move(futures));

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

async_simple::coro::Lazy<std::vector<VectorSearchResult>>
indexer::vector_search(const std::string &query_text, int top_k) {
  if (!ctx.embedding_service_ || !ctx.vector_db_service_) {
    ELOGFMT(ERROR, "Vector search failed: embedding service or vector database "
                   "not available");
    co_return std::vector<VectorSearchResult>{};
  }

  ELOGFMT(INFO, "Performing vector search with query: {}", query_text);

  Content content;
  content.text = query_text;

  try {
    auto embedding = co_await ctx.embedding_service_->multimodal_embedding(
        std::move(content));

    if (!embedding) {
      ELOGFMT(ERROR, "Failed to generate embedding for query text");
      co_return std::vector<VectorSearchResult>{};
    }

    auto search_results = ctx.vector_db_service_->Search(
        embedding.value()[EmbeddingType::Text], top_k);
    ELOGFMT(INFO, "Vector search found {} results", search_results.size());

    co_return co_await process_search_results(search_results);
  } catch (const std::exception &e) {
    ELOGFMT(ERROR, "Error during vector search: {}", e.what());
    co_return std::vector<VectorSearchResult>{};
  }
}

async_simple::coro::Lazy<std::vector<VectorSearchResult>>
indexer::vector_search_image(const std::string &image_path, int top_k) {
  if (!ctx.embedding_service_ || !ctx.vector_db_service_) {
    ELOGFMT(ERROR, "Vector search failed: embedding service or vector database "
                   "not available");
    co_return std::vector<VectorSearchResult>{};
  }

  if (!ctx.embedding_service_->support_aligned_image()) {
    ELOGFMT(ERROR, "Vector search failed: embedding service does not support "
                   "image embedding");
    co_return std::vector<VectorSearchResult>{};
  }

  ELOGFMT(INFO, "Performing vector search with image: {}", image_path);

  Content image_content;
  image_content.image_path = image_path;

  try {

    auto embedding = co_await ctx.embedding_service_->multimodal_embedding(
        std::move(image_content));

    if (!embedding) {
      ELOGFMT(ERROR, "Failed to generate embedding for image");
      co_return std::vector<VectorSearchResult>{};
    }

    auto search_results = ctx.vector_db_service_->Search(
        embedding.value()[EmbeddingType::Image], top_k);
    ELOGFMT(INFO, "Vector search found {} results", search_results.size());

    co_return co_await process_search_results(search_results);
  } catch (const std::exception &e) {
    ELOGFMT(ERROR, "Error during vector search: {}", e.what());
    co_return std::vector<VectorSearchResult>{};
  }
}

async_simple::coro::Lazy<std::vector<VectorSearchResult>>
indexer::vector_search_multimodal(const std::string &query_text,
                                  const std::string &image_path, int top_k) {
  if (!ctx.embedding_service_ || !ctx.vector_db_service_) {
    ELOGFMT(ERROR, "Vector search failed: embedding service or vector database "
                   "not available");
    co_return std::vector<VectorSearchResult>{};
  }

  if (!ctx.embedding_service_->support_aligned_image()) {
    ELOGFMT(ERROR, "Vector search failed: embedding service does not support "
                   "image embedding");
    co_return std::vector<VectorSearchResult>{};
  }

  ELOGFMT(INFO, "Performing multimodal vector search with text and image");

  Content content;

  content.text = query_text;
  content.image_path = image_path;

  try {
    auto embedding = co_await ctx.embedding_service_->multimodal_embedding(
        std::move(content));

    if (!embedding) {
      ELOGFMT(ERROR, "Failed to generate embedding for multimodal query");
      co_return std::vector<VectorSearchResult>{};
    }

    auto search_results = ctx.vector_db_service_->Search(
        embedding.value()[EmbeddingType::Text], top_k);
    ELOGFMT(INFO, "Vector search found {} results", search_results.size());

    co_return co_await process_search_results(search_results);
  } catch (const std::exception &e) {
    ELOGFMT(ERROR, "Error during vector search: {}", e.what());
    co_return std::vector<VectorSearchResult>{};
  }
}

async_simple::coro::Lazy<std::vector<VectorSearchResult>>
indexer::process_search_results(const std::vector<SearchResult> &results) {
  std::vector<VectorSearchResult> processed_results;

  for (const auto &result : results) {

    std::string message_id_str = result.key.substr(0, result.key.find(':'));

    auto msg_opt = ctx.message_db.get(message_id_str);
    if (!msg_opt) {
      ELOGFMT(WARNING,
              "Message {} found in vector database but not in message database",
              message_id_str);
      continue;
    }

    processed_results.push_back(
        VectorSearchResult{.msg = msg_opt.value(), .score = result.score});
  }

  ELOGFMT(INFO, "Processed {} search results", processed_results.size());
  co_return processed_results;
}
} // namespace tgdb
