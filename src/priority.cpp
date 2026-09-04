#include "workload_fabric/priority.hpp"

namespace wf {
namespace {
inline constexpr const char* kTier[] = {"LOWEST","LOW","NORMAL","HIGH","CRITICAL","SYSTEM"};
}  // namespace

std::string_view priorityTierName(PriorityTier t) noexcept {
  auto i = static_cast<std::size_t>(t);
  return i < 6 ? kTier[i] : "INVALID";
}

void Priority::setBase(PriorityClass cls, PriorityTier tier, int priority) noexcept {
  cls_ = cls;
  baseTier_ = tier;
  baseValue_ = priority;
  gen_ = next(gen_);
}
void Priority::promote(int delta) noexcept {
  if (delta <= 0) return;
  promotion_ += delta;
  gen_ = next(gen_);
}
void Priority::demote(int delta) noexcept {
  if (delta <= 0) return;
  promotion_ -= delta;
  gen_ = next(gen_);
}
void Priority::markStarved() noexcept { starved_ = true; gen_ = next(gen_); }

int Priority::effectiveValue() const noexcept {
  int v = baseValue_ + promotion_;
  if (starved_) v += 1000;  // starvation protection: deterministic large boost
  return v;
}

}  // namespace wf
