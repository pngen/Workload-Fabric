#pragma once
// Strongly-typed identity wrapper. Each identity is a distinct C++ type over a
// 64-bit value so that semantic domains can never be silently mixed (a
// WorkloadId can never be passed where an ExecutionEpisodeId is expected).
#include <cstdint>
#include <compare>
#include <type_traits>

namespace wf::detail {

template <class Tag, class Value = std::uint64_t>
class UniqueValue {
 public:
  using ValueType = Value;
  using TagType   = Tag;

  constexpr UniqueValue() noexcept = default;
  constexpr explicit UniqueValue(Value v) noexcept : value_(v) {}
  constexpr UniqueValue(UniqueValue const&) noexcept            = default;
  constexpr UniqueValue& operator=(UniqueValue const&) noexcept = default;
  constexpr UniqueValue(UniqueValue&&) noexcept                 = default;
  constexpr UniqueValue& operator=(UniqueValue&&) noexcept      = default;

  constexpr Value value() const noexcept { return value_; }
  constexpr bool valid() const noexcept { return value_ != Value{0}; }
  constexpr explicit operator bool() const noexcept { return valid(); }

  constexpr Value toU64() const noexcept { return static_cast<Value>(value_); }
  constexpr UniqueValue previous() const noexcept { return UniqueValue{static_cast<Value>(value_ - 1)}; }
  static constexpr UniqueValue fromU64(std::uint64_t v) noexcept { return UniqueValue{static_cast<Value>(v)}; }

  friend constexpr bool operator==(UniqueValue a, UniqueValue b) noexcept { return a.value_ == b.value_; }
  friend constexpr bool operator!=(UniqueValue a, UniqueValue b) noexcept { return a.value_ != b.value_; }
  friend constexpr auto operator<=>(UniqueValue a, UniqueValue b) noexcept { return a.value_ <=> b.value_; }

 private:
  Value value_{};
};

// Generation: monotonic counter used to fence stale authority within one
// domain. Generations only ever increase; they are not globally unique.
template <class Tag>
using Generation = UniqueValue<Tag, std::uint64_t>;

}  // namespace wf::detail
