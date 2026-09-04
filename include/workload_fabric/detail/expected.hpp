#pragma once
// Minimal self-contained expected<T,E>. C++23 std::expected is not guaranteed
// on every C++20 toolchain, so we supply a small equivalent.
#include <utility>
#include <variant>
#include <stdexcept>
#include <type_traits>

namespace wf::detail {

struct BadExpectedAccess : std::runtime_error {
  using std::runtime_error::runtime_error;
};

template <class T, class E>
class expected {
 public:
  static_assert(!std::is_void_v<T>, "expected<void,E> not supported");

  expected(T val) : store_(std::in_place_index<0>, std::move(val)) {}
  expected(E err) : store_(std::in_place_index<1>, std::move(err)) {}
  expected(expected const&) = default;
  expected(expected&&)      = default;
  expected& operator=(expected const&) = default;
  expected& operator=(expected&&)      = default;

  bool has_value() const noexcept { return store_.index() == 0; }
  explicit operator bool() const noexcept { return has_value(); }

  T& value() & { unchecked(0); return std::get<0>(store_); }
  T const& value() const& { unchecked(0); return std::get<0>(store_); }
  T&& value() && { unchecked(0); return std::move(std::get<0>(store_)); }

  E& error() & { unchecked(1); return std::get<1>(store_); }
  E const& error() const& { unchecked(1); return std::get<1>(store_); }
  E&& error() && { unchecked(1); return std::move(std::get<1>(store_)); }

  template <class U>
  T value_or(U&& dflt) const& { return has_value() ? value() : static_cast<T>(std::forward<U>(dflt)); }

  T const& operator*() const& { return value(); }
  T& operator*() & { return value(); }
  T const* operator->() const { return &value(); }
  T* operator->() { return &value(); }

 private:
  void unchecked(std::size_t want) const {
    if (store_.index() != want) throw BadExpectedAccess("expected: accessed wrong alternative");
  }
  std::variant<T, E> store_;
};

}  // namespace wf::detail
