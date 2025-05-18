#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <ylt/coro_http/coro_http_client.hpp>
#include <ylt/struct_pack.hpp>
#include <expected>

namespace tgdb {
struct ocr_client {
  async_simple::coro::Lazy<std::expected<std::string, std::string>> ocr(std::string file_path);

private:
  const std::string url_ = "http://127.0.0.1:9003/ocr";
};
} // namespace tgdb