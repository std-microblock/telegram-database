#pragma once
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>

namespace tgdb {
struct user {
  std::string nickname;
  int64_t user_id;
  std::optional<std::string> str_id;
  inline std::string to_string() const {
    return std::format("user{{nickname: {}, user_id: {}, str_id: {}}}",
                       nickname, user_id, str_id.value_or("None"));
  }
};
struct message {
  int64_t message_id;
  int64_t send_time;
  int64_t chat_id;
  std::unordered_map<std::string, std::string> textifyed_contents = {};
  user sender;
  int64_t reply_to_message_id = -1;

  inline std::string to_string() const {
    return std::format("message{{message_id: {}, "
                       "textifyed_contents: {}, "
                       "sender: {}, reply_to_message_id: {}}}",
                       message_id, textifyed_contents,
                       sender.to_string(), reply_to_message_id);
  }
};
} // namespace tgdb