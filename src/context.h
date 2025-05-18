#pragma once
#include "bot.h"
#include "config.h"
#include "data.h"
#include "database/database.hpp"
#include "indexer.h"
#include "ocr.h"
namespace tgdb {
struct context {
  kvdb::database<message> message_db;
  config cfg;
  bot bot{*this};
  indexer indexer{*this};
  ocr_client ocr_client;
  context() : message_db("message_db") {}
  void init();
};
} // namespace tgdb