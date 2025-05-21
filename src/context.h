#pragma once
#include "bot.h"
#include "config.h"
#include "data.h"
#include "database/database.hpp"
#include "database/vector_db.h"
#include "embedding/embedding_service.h"
#include "indexer.h"
#include "ocr.h"
#include <memory>


namespace tgdb {
struct context {
  kvdb::database<message> message_db;
  config cfg;
  bot bot{*this};
  indexer indexer{*this};
  std::unique_ptr<IOcrClient> ocr_client_ = nullptr;
  std::unique_ptr<VectorDbService> vector_db_service_ = nullptr;
  std::unique_ptr<EmbeddingService> embedding_service_ = nullptr;
  context() : message_db("message_db") {}
  void init();
};
} // namespace tgdb