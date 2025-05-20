#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <ylt/coro_http/coro_http_client.hpp>
#include <ylt/struct_pack.hpp>
#include <expected>

namespace tgdb {

class IOcrClient {
public:
  virtual ~IOcrClient() = default;
  virtual async_simple::coro::Lazy<std::expected<std::string, std::string>> ocr(std::string file_path) = 0;
};

struct OcrClient : public IOcrClient {
  OcrClient(std::string url) : url_(std::move(url)) {}
  async_simple::coro::Lazy<std::expected<std::string, std::string>> ocr(std::string file_path) override;

private:
  const std::string url_;
};

struct NullOcrClient : public IOcrClient {
  async_simple::coro::Lazy<std::expected<std::string, std::string>> ocr(std::string file_path) override;
};

} // namespace tgdb