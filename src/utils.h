#pragma once
#include <chrono>
#include <format>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "async_simple/Promise.h"
#include "async_simple/coro/Sleep.h"
#include "td/telegram/td_api.h"

#include "async_simple/coro/Lazy.h"
#include "cinatra/ylt/coro_io/io_context_pool.hpp"

#include "ylt/easylog.hpp"

namespace tgdb {

namespace detail {
template <class... Fs> struct overload;

template <class F> struct overload<F> : public F {
  explicit overload(F f) : F(f) {}
};
template <class F, class... Fs>
struct overload<F, Fs...> : public overload<F>, public overload<Fs...> {
  overload(F f, Fs... fs) : overload<F>(f), overload<Fs...>(fs...) {}
  using overload<F>::operator();
  using overload<Fs...>::operator();
};
} // namespace detail

template <class... F> auto _overloaded(F... f) {
  return detail::overload<F...>(f...);
}

auto overloaded(auto &&...f) {
  return _overloaded(std::forward<decltype(f)>(f)..., [](auto &) {});
}

template <typename T> td::td_api::object_ptr<T> try_move_as(auto &object) {
  if (object->get_id() != T::ID) {
    return nullptr;
  }
  return td::td_api::move_object_as<T>(std::move(object));
}

template <typename T> T *try_get_as(auto &object) {
  if (object->get_id() != T::ID) {
    return nullptr;
  }
  return static_cast<T *>(object.get());
}

template <typename T, typename U> struct task_batch_debounce_pool {
  std::vector<T> tasks = {};
  std::vector<std::shared_ptr<async_simple::Promise<U>>> promises = {};
  std::chrono::milliseconds debounce_time;
  std::function<async_simple::coro::Lazy<std::vector<U>>(std::vector<T>)>
      task_handler;

  task_batch_debounce_pool(std::chrono::milliseconds debounce_time,
                           decltype(task_handler) task_handler)
      : debounce_time(debounce_time), task_handler(std::move(task_handler)) {}

  async_simple::coro::Lazy<U> add_task(T task) {
    auto promise = std::make_shared<async_simple::Promise<U>>();

    std::unique_lock<std::recursive_mutex> lock(mutex_);

    tasks.push_back(std::move(task));
    promises.push_back(promise);

    if (!has_pending_debounce) {
      has_pending_debounce = true;
      lock.unlock();
      ELOGFMT(DEBUG, "Starting debounce timer for {} ms", debounce_time.count());
      co_await async_simple::coro::sleep(debounce_time);
      ELOGFMT(DEBUG, "Debounce timer finished, processing {} tasks",
              tasks.size());
      lock.lock();
      auto tasks_to_process = std::move(tasks);
      auto promises_to_process = std::move(promises);
      tasks = std::vector<T>();
      promises = {};
      has_pending_debounce = false;

      auto results = co_await task_handler(std::move(tasks_to_process));
      assert(results.size() == promises_to_process.size());
      for (size_t i = 0; i < promises_to_process.size(); ++i) {
        promises_to_process[i]->setValue(std::move(results[i]));
      }
    }

    lock.unlock();
    co_return co_await promise->getFuture().via(coro_io::get_global_executor());
  }

private:
  bool has_pending_debounce = false;
  std::recursive_mutex mutex_ = {};
};
}; // namespace tgdb