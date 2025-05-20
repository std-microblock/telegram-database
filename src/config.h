#pragma once
#include <string>
#include <optional>

namespace tgdb {
struct config {
  std::string bot_token;
  int64_t chat_id;
  std::string api_hash = "a3406de8d171bb422bb6ddf3bbd800e2";
  int64_t api_id = 94575;
  std::string device_model = "Desktop";


  struct ocr_config_t {
    std::string api_url;
  };

  std::optional<ocr_config_t> ocr_config;
};
} // namespace tgdb