#include "workload_fabric/dependency.hpp"

namespace wf {
namespace {
inline constexpr const char* kReq[] = {"REQUIRED","OPTIONAL"};
inline constexpr const char* kReady[] = {"COMPLETED","READY","ARTIFACT_READY"};
inline constexpr const char* kDepSt[] = {"PENDING","SATISFIED","FAILED","INVALIDATED","STALE"};
}  // namespace
std::string_view dependencyRequirementName(DependencyRequirement r) noexcept { return static_cast<std::size_t>(r) < 2 ? kReq[static_cast<std::size_t>(r)] : "INVALID"; }
std::string_view dependencyReadyName(DependencyReady r) noexcept { return static_cast<std::size_t>(r) < 3 ? kReady[static_cast<std::size_t>(r)] : "INVALID"; }
std::string_view dependencyStatusName(DependencyStatus s) noexcept { return static_cast<std::size_t>(s) < 5 ? kDepSt[static_cast<std::size_t>(s)] : "INVALID"; }

Outcome DependencySet::define(const DependencyDef& d) noexcept {
  defs_.push_back(d);
  states_.emplace_back();
  gen_ = next(gen_);
  return Outcome::ALLOW;
}
Outcome DependencySet::clear() noexcept {
  defs_.clear(); states_.clear(); gen_ = next(gen_);
  return Outcome::ALLOW;
}
Outcome DependencySet::setStatus(std::size_t index, DependencyStatus status, DependencyGeneration observed) noexcept {
  if (index >= states_.size()) return Outcome::REJECT_UNKNOWN_DEPENDENCY;
  // Reject a downgrade reported by a stale observer: if a later generation already
  // observed this dependency, an older observed generation must not overwrite it.
  if (observed.toU64() < states_[index].observedGeneration.toU64()) return Outcome::REJECT_STALE_DEPENDENCY;
  states_[index].status = status;
  states_[index].observedGeneration = observed;
  return Outcome::ALLOW;
}
bool DependencySet::ready() const noexcept {
  for (std::size_t i = 0; i < defs_.size(); ++i) {
    if (defs_[i].requirement != DependencyRequirement::REQUIRED) continue;
    if (states_[i].status != DependencyStatus::SATISFIED) return false;
  }
  return true;
}
std::optional<std::size_t> DependencySet::firstUnmetRequired() const noexcept {
  for (std::size_t i = 0; i < defs_.size(); ++i) {
    if (defs_[i].requirement != DependencyRequirement::REQUIRED) continue;
    if (states_[i].status != DependencyStatus::SATISFIED) return i;
  }
  return std::nullopt;
}

}  // namespace wf
