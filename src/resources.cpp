#include "workload_fabric/resource.hpp"

namespace wf {

namespace {
inline constexpr const char* kNeed[] = {"REQUIRED","PREFERRED","OPTIONAL","PROHIBITED"};
inline constexpr const char* kKind[] = {
  "ACCELERATOR_COUNT","ACCELERATOR_CAPABILITY_CLASS","DEVICE_MEMORY","HOST_MEMORY","PINNED_MEMORY","CPU_CAPACITY",
  "STORAGE","TRANSFER_BANDWIDTH","NETWORK_REQUIREMENT","MODEL_RESIDENCY","LOCALITY_CONSTRAINT",
  "EXECUTION_CONCURRENCY","DEADLINE_SLO_CLASS","MAX_RESTART_COUNT","MAX_RECOVERY_DURATION",
  "CHECKPOINT_REQUIREMENT","MIGRATION_ELIGIBILITY"};
inline constexpr const char* kGrant[] = {"REQUESTED","GRANTED","BOUND","RELEASED","STALE","EXPIRED"};
}  // namespace

std::string_view needLevelName(NeedLevel l) noexcept {
  auto i = static_cast<std::size_t>(l);
  return i < 4 ? kNeed[i] : "INVALID";
}
std::string_view resourceKindName(ResourceKind k) noexcept {
  auto i = static_cast<std::size_t>(k);
  return i < 17 ? kKind[i] : "INVALID";
}
std::string_view grantStatusName(GrantStatus s) noexcept {
  auto i = static_cast<std::size_t>(s);
  return i < 6 ? kGrant[i] : "INVALID";
}

Outcome ResourceContract::add(ResourceRequirement req) noexcept {
  // PROHIBITED and OPTIONAL can coexist; a PROHIBITED requirement never carries value.
  if (req.level == NeedLevel::PROHIBITED) req.minValue = 0.0, req.maxValue = 0.0;
  for (const auto& existing : reqs_) {
    // Reject silent level changes for the same dimension: a preference never becomes
    // a requirement merely by being added twice.
    if (existing.kind == req.kind && existing.spec == req.spec) return Outcome::REJECT_IMMUTABLE_CONTRACT;
  }
  reqs_.push_back(std::move(req));
  generation_ = next(generation_);
  return Outcome::ALLOW;
}

Outcome ResourceContract::setRequirements(const std::vector<ResourceRequirement>& reqs) noexcept {
  for (const auto& r : reqs) {
    if (r.level != NeedLevel::REQUIRED && r.level != NeedLevel::PREFERRED &&
        r.level != NeedLevel::OPTIONAL && r.level != NeedLevel::PROHIBITED) return Outcome::REJECT_IMMUTABLE_CONTRACT;
  }
  reqs_ = reqs;
  generation_ = next(generation_);
  return Outcome::ALLOW;
}

Outcome ResourceContract::applyGrant(ResourceGrant g) noexcept {
  g.status = g.contractGeneration == generation_ ? GrantStatus::GRANTED : GrantStatus::STALE;
  grant_ = std::move(g);
  return Outcome::ALLOW;
}

Outcome ResourceContract::releaseGrant(ResourceContractGeneration expecting) noexcept {
  if (expecting != generation_) return Outcome::REJECT_STALE_RESOURCE_CONTRACT;
  if (grant_.released) return Outcome::REJECT_RESOURCE_DOUBLE_RELEASE;
  grant_.released = true;
  grant_.status = GrantStatus::RELEASED;
  return Outcome::ALLOW;
}

Outcome ResourceContract::advanceGeneration(ResourceContractGeneration expecting) noexcept {
  if (expecting != generation_) return Outcome::REJECT_STALE_RESOURCE_CONTRACT;
  generation_ = next(generation_);
  return Outcome::ALLOW;
}

bool ResourceContract::satisfied() const noexcept {
  // A stale/expired/released grant never satisfies the current contract generation.
  if (grant_.status != GrantStatus::GRANTED || grant_.released) return false;
  for (const auto& req : reqs_) {
    if (req.level != NeedLevel::REQUIRED) continue;
    bool found = false;
    for (const auto& b : grant_.bindings) {
      if (b.kind == req.kind && req.satisfies(b.amount)) { found = true; break; }
    }
    if (!found) return false;
  }
  // PROHIBITED resources must have no binding.
  for (const auto& req : reqs_) {
    if (req.level != NeedLevel::PROHIBITED) continue;
    for (const auto& b : grant_.bindings) {
      if (b.kind == req.kind && b.amount > 0.0) return false;
    }
  }
  return true;
}

std::optional<ResourceRequirement> ResourceContract::firstUnsatisfiedRequired() const noexcept {
  for (const auto& req : reqs_) {
    if (req.level != NeedLevel::REQUIRED) continue;
    bool found = false;
    for (const auto& b : grant_.bindings) {
      if (b.kind == req.kind && req.satisfies(b.amount)) { found = true; break; }
    }
    if (!found) return req;
  }
  return std::nullopt;
}

}  // namespace wf
