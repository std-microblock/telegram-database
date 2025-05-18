#pragma once
#include "td/telegram/td_api.h"
#include <memory>
#include <utility>

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

template <typename T>
td::td_api::object_ptr<T> try_move_as(auto& object) {
  if (object->get_id() != T::ID) {
    return nullptr;
  }
  return td::td_api::move_object_as<T>(std::move(object));
}

template <typename T>
T* try_get_as(auto& object) {
  if (object->get_id() != T::ID) {
    return nullptr;
  }
  return static_cast<T*>(object.get());
}
}; // namespace tgdb