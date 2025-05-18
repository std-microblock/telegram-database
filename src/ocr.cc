#include "ocr.h"
#include "cinatra/coro_http_client.hpp"
#include "cinatra/define.h"
#include "rfl/json.hpp"
#include "rfl/json/read.hpp"
#include "ylt/easylog.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace tgdb {

struct ocr_box {
  std::string rec_txt;
  std::vector<std::vector<float>> dt_boxes;
};

using ocr_response = std::map<std::string, ocr_box>;

async_simple::coro::Lazy<std::expected<std::string, std::string>>
ocr_client::ocr(std::string file_path) {

  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    ELOGFMT(ERROR, "Failed to open file: {}", file_path);
    co_return std::unexpected<std::string>("Failed to open file");
  }

  std::string file_data((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

  std::string boundary = "--CinatraBoundary2B8FAF4A80EDB307";
  std::string body =
      "--" + boundary + "\r\n" +
      "Content-Disposition: form-data; name=\"image_file\"; filename=\"" +
      file_path + "\"\r\n" + "Content-Type: image/webp\r\n\r\n" + file_data +
      "\r\n" + "--" + boundary + "--\r\n";

  std::string content_type = "multipart/form-data; boundary=" + boundary;

  int retry_count = 0;
retry:
  coro_http::coro_http_client client;
  client.set_conn_timeout(std::chrono::seconds(10));
  client.set_req_timeout(std::chrono::seconds(50));
  auto result = co_await client.async_post(
      url_, body, cinatra::req_content_type::multipart,
      {{"Content-Type", content_type},
       {"Content-Length", std::to_string(body.size())}});

  if (result.net_err) {
    ELOGFMT(ERROR, "Error: {}", result.net_err.message());
    if (retry_count < 3) {
      retry_count++;
      ELOGFMT(WARN, "Retrying OCR request, attempt {}", retry_count);
      goto retry;
    }

    co_return std::unexpected<std::string>(result.net_err.message());
  }
  if (auto ocr_resp = rfl::json::read<ocr_response>(result.resp_body);
      ocr_resp) {
    std::string recognized_text;
    for (const auto &pair : ocr_resp.value()) {
      recognized_text += pair.second.rec_txt + "\n";
    }

    co_return recognized_text;
  } else {
    ELOGFMT(ERROR, "Failed to deserialize OCR response: {}",
            ocr_resp.error().what());

    if (retry_count < 3) {
      retry_count++;
      ELOGFMT(WARN, "Retrying OCR request, attempt {}", retry_count);
      goto retry;
    }
    co_return std::unexpected<std::string>(
        "Failed to deserialize OCR response");
  }
}
} // namespace tgdb