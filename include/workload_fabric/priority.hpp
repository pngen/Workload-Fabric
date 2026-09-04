#pragma once
// Explicit priority semantics. Workload Fabric computes/exposes deterministic
// *workload* priority state; cross-workload placement/dispatch is external.
#include <cstdint>
#include <string_view>
#include "workload_fabric/identity.hpp"

namespace wf {

// A coarse, ordered priority tier. Deterministic and stable.
enum class PriorityTier : std::uint8_t {
  LOWEST = 0, LOW = 1, NORMAL = 2, HIGH = 3, CRITICAL = 4, SYSTEM = 5,
};
std::string_view priorityTierName(PriorityTier t) noexcept;
constexpr int tierRank(PriorityTier t) noexcept { return static_cast<int>(t); }

// A workload's priority. Carries an immutable identity (PriorityClass), a
// generation-fenced mutable part, and a deterministic *effective* priority that
// accounts for base priority, explicit promotion/demotion, age, starvation
// protection, and deadline urgency.
class Priority {
 public:
  Priority() = default;

  void setBase(PriorityClass cls, PriorityTier tier, int priority) noexcept;

  PriorityClass    cls() const noexcept { return cls_; }
  PriorityGeneration generation() const noexcept { return gen_; }
  PriorityTier     baseTier() const noexcept { return baseTier_; }
  int              baseValue() const noexcept { return baseValue_; }
  int              promotion() const noexcept { return promotion_; }
  bool             starved() const noexcept { return starved_; }
  bool             preemptible() const noexcept { return preemptible_; }

  // Promote/demote the effective priority (generation-advancing).
  void promote(int delta) noexcept;
  void demote(int delta) noexcept;
  void markStarved() noexcept;

  // Deterministic effective priority value. Larger = higher priority.
  int effectiveValue() const noexcept;

  // Preemption eligibility: CRITICAL/SYSTEM and non-preemptible workloads are
  // never preempted by definition of this runtime's contract.
  bool mayBePreempted() const noexcept { return preemptible_ && !protected_; }

  void setProtected(bool b) noexcept { protected_ = b; }
  void setForTest(int base, int promotion) noexcept { baseValue_ = base; promotion_ = promotion; }
  bool protectedFromPreemption() const noexcept { return protected_; }

  // persistence/reconstruction support
  void setPriorityGen(PriorityGeneration g) noexcept { gen_ = g; }
  void setBaseTier(PriorityTier t) noexcept { baseTier_ = t; }
  void setPriorityInts(int base, int promo) noexcept { baseValue_ = base; promotion_ = promo; }
  void setFlagsForTest(bool starved, bool preemptible, bool protectedFlag) noexcept { starved_ = starved; preemptible_ = preemptible; protected_ = protectedFlag; }

 private:
  PriorityClass     cls_;
  PriorityGeneration gen_;
  PriorityTier      baseTier_ = PriorityTier::NORMAL;
  int               baseValue_ = 0;
  int               promotion_ = 0;
  bool              starved_ = false;
  bool              preemptible_ = true;
  bool              protected_ = false;
};

}  // namespace wf
