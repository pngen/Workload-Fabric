#pragma once
// Deterministic outcome taxonomy. Every mutation, transition, or decision
// resolves to exactly one outcome. Unknown stays unknown: the runtime never
// fabricates an answer it does not actually know.
#include <cstdint>
#include <string_view>

namespace wf {

enum class Outcome : std::uint8_t {
  ALLOW                            = 0,
  DEFER                            = 1,
  BLOCKED_DEPENDENCY               = 2,
  BLOCKED_RESOURCE_CONTRACT        = 3,

  REJECT_STALE_EPOCH               = 4,
  REJECT_STALE_BOOT                = 5,
  REJECT_STALE_WORKLOAD_GENERATION = 6,
  REJECT_STALE_EPISODE             = 7,
  REJECT_STALE_RESOURCE_CONTRACT   = 8,
  REJECT_STALE_DEPENDENCY          = 9,
  REJECT_STALE_PRIORITY            = 10,
  REJECT_STALE_PROGRESS            = 11,
  REJECT_STALE_MIGRATION           = 12,
  REJECT_TERMINAL                  = 13,
  REJECT_CANCELLED                 = 14,
  REJECT_RESTART_LIMIT             = 15,
  REJECT_MIGRATION_INELIGIBLE      = 16,
  REJECT_NOT_SUSPENDED             = 17,
  REJECT_NOT_RESUMABLE             = 18,
  REJECT_INVALID_TRANSITION        = 19,
  REJECT_PROGRESS_DOUBLE_COUNT     = 20,
  REJECT_PROGRESS_REGRESSION       = 21,
  REJECT_RESOURCE_DOUBLE_RELEASE   = 22,
  REJECT_DUPLICATE_WORKLOAD        = 23,
  REJECT_WORKLOAD_NOT_FOUND        = 24,
  REJECT_CROSS_WORKLOAD            = 25,
  REJECT_IMMUTABLE_CONTRACT        = 26,
  REJECT_PRIORITY_NOT_MUTABLE      = 27,
  REJECT_NOT_BLOCKED               = 28,
  REJECT_EPISODE_INELIGIBLE        = 29,
  REJECT_NOT_RUNNABLE              = 30,
  REJECT_UNKNOWN_DEPENDENCY        = 31,
  REJECT_DEGRADED_POLICY           = 32,
  REJECT_COMPLETION_POLICY         = 33,
  REJECT_UNKNOWN                   = 34,
  REVALIDATION_REQUIRED            = 35,
  UNKNOWN                          = 36,
};

constexpr bool isAllow(Outcome o) noexcept { return o == Outcome::ALLOW; }
constexpr bool isReject(Outcome o) noexcept {
  auto v = static_cast<int>(o);
  return v >= static_cast<int>(Outcome::REJECT_STALE_EPOCH) && v <= static_cast<int>(Outcome::REJECT_UNKNOWN);
}
constexpr bool isBlocked(Outcome o) noexcept {
  return o == Outcome::BLOCKED_DEPENDENCY || o == Outcome::BLOCKED_RESOURCE_CONTRACT;
}

std::string_view outcomeName(Outcome o) noexcept;

}  // namespace wf
