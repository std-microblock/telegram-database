#pragma once
#include "bot.h"
#include "config.h"
#include "data.h"
#include "database/database.hpp"
#include "indexer.h"
#include "ocr.h"
#include "vector_db.h" // Added
#include <memory>

namespace tgdb {
struct context {
  kvdb::database<message> message_db;
  config cfg;
  bot bot{*this};
  indexer indexer{*this};
  std::unique_ptr<IOcrClient> ocr_client_;
  std::unique_ptr<VectorDbService> vector_db_service_; // Added
  context() : message_db("message_db") {}
  void init();
};
} // namespace tgdb