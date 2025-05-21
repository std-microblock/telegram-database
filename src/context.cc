#include "context.h"
#include "config.h"

#include <filesystem>

#include "rfl/DefaultIfMissing.hpp"
#include "rfl/json/read.hpp"
#include "ylt/easylog.hpp"

#include "database/faiss_vector_db.h"

void tgdb::context::init() {
  if (std::filesystem::exists("./config.json")) {
    auto ifs = std::ifstream("./config.json");
    if (!ifs) {
      ELOGFMT(ERROR, "Failed to open config.json");
      throw std::runtime_error("Failed to open config.json");
    }

    std::string json_str((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    auto config =
        rfl::json::read<tgdb::config, rfl::DefaultIfMissing>(json_str);
    if (config) {
      cfg = config.value();
      ELOGFMT(INFO, "Loaded config: {}", json_str);
    } else {
      ELOGFMT(ERROR, "Failed to load config: {}", config.error().what());
      throw std::runtime_error("Failed to load config: " +
                               config.error().what());
    }
  } else {
    ELOGFMT(WARNING, "No config.json found, using default configuration");
  }

  if (auto res = message_db.open(); !res) {
    ELOGFMT(ERROR, "Failed to open message_db: {}", res.error());
    throw std::runtime_error("Failed to open message_db: " + res.error());
  } else {
    ELOGFMT(INFO, "message_db opened successfully, {} entries",
            message_db.cache->size());
  }

  if (cfg.vector_database == "faiss") {
    vector_db_service_ =
        std::make_unique<FaissVectorDbService>(1024, faiss::METRIC_L2);
    if (!vector_db_service_->Load("vector_db.faiss")) {
      ELOGFMT(ERROR, "Failed to load vector database!");
      return;
    } else {
      ELOGFMT(INFO, "vector_db loaded successfully");
    }
  } else {
    ELOGFMT(WARNING,
            "Invalid vector database found in config, vector database won't be used.");
  }

  bot.init();
}
