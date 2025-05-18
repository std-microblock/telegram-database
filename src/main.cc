#include "context.h"
#include "database/database.hpp"
#include "ylt/easylog.hpp"
#include <print>

int main() {
  easylog::init_log(easylog::Severity::INFO, "tgdb.log", true, true, 40'000'000, 3, true);
  
  ELOGFMT(INFO, "Telegram Database Bot");

  tgdb::context ctx;
  ctx.init();

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return 0;
}