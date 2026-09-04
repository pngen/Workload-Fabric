#pragma once
// Resource contracts: what a workload requires or has been granted, across its
// lifecycle. Resource Broker owns arbitration; Workload Fabric owns the *contract*
// and its lifecycle relationship to the workload. Everything is generation-fenced.
#include <cstdint>
#include <string>
#include <vector>
#include <string_view>
#include <optional>
#include "workload_fabric/outcome.hpp"
#include "workload_fabric/identity.hpp"

namespace wf {

// Whether a resource dimension is hard-required, merely preferred, allowed if
// present, or explicitly prohibited. A preference is never silently promoted to
// a requirement, and vice versa.
enum class NeedLevel : std::uint8_t { REQUIRED = 0, PREFERRED = 1, OPTIONAL = 2, PROHIBITED = 3 };
std::string_view needLevelName(NeedLevel l) noexcept;

// Resource dimensions a workload can express. This is not an exhaustive
// hardware taxonomy; it is the workload-facing contract vocabulary.
enum class ResourceKind : std::uint8_t {
  ACCELERATOR_COUNT = 0,
  ACCELERATOR_CAPABILITY_CLASS = 1,
  DEVICE_MEMORY = 2,
  HOST_MEMORY = 3,
  PINNED_MEMORY = 4,
  CPU_CAPACITY = 5,
  STORAGE = 6,
  TRANSFER_BANDWIDTH = 7,
  NETWORK_REQUIREMENT = 8,
  MODEL_RESIDENCY = 9,
  LOCALITY_CONSTRAINT = 10,
  EXECUTION_CONCURRENCY = 11,
  DEADLINE_SLO_CLASS = 12,
  MAX_RESTART_COUNT = 13,
  MAX_RECOVERY_DURATION = 14,
  CHECKPOINT_REQUIREMENT = 15,
  MIGRATION_ELIGIBILITY = 16,
};
std::string_view resourceKindName(ResourceKind k) noexcept;

// The value of a resource requirement. Numeric dimensions use the numeric field;
// descriptive dimensions (capability class, locality, network, model residency)
// use the spec string. Prohibited dimensions carry only a level.
struct ResourceRequirement {
  ResourceKind      kind = ResourceKind::ACCELERATOR_COUNT;
  NeedLevel         level = NeedLevel::REQUIRED;
  double            minValue = 0.0;
  double            maxValue = 0.0;   // 0 == exact match on minValue
  std::string       spec;             // capability class / locality / network / residency descriptor

  bool satisfies(double v) const noexcept {
    if (level == NeedLevel::PROHIBITED) return v == 0.0;
    if (v < minValue) return false;
    if (maxValue > 0.0 && v > maxValue) return false;
    return true;
  }
};

// The lifecycle of a resource grant relative to a contract generation.
enum class GrantStatus : std::uint8_t {
  REQUESTED = 0, GRANTED = 1, BOUND = 2, RELEASED = 3, STALE = 4, EXPIRED = 5,
};
std::string_view grantStatusName(GrantStatus s) noexcept;

// A single granted resource binding (a concrete allocation assignment).
struct ResourceBinding {
  ResourceKind kind = ResourceKind::ACCELERATOR_COUNT;
  double       amount = 0.0;
  std::string  qualifier;   // e.g. concrete device id / node id
};

// A grant corresponds to one contract generation. If the contract advances, the
// old grant becomes STALE and no longer binds current execution.
struct ResourceGrant {
  ResourceContractId          contractId;
  ResourceContractGeneration  contractGeneration;
  GrantStatus                 status = GrantStatus::REQUESTED;
  std::vector<ResourceBinding> bindings;
  bool released = false;
};

// The durable resource contract for a workload. Once created its identity is
// immutable; every mutation produces a new ResourceContractGeneration.
class ResourceContract {
 public:
  ResourceContract() = default;
  ResourceContract(ResourceContractId id, ResourceContractGeneration gen)
      : id_(id), generation_(gen) {}

  ResourceContractId          id() const noexcept { return id_; }
  ResourceContractGeneration  generation() const noexcept { return generation_; }
  const std::vector<ResourceRequirement>& requirements() const noexcept { return reqs_; }
  // The current (bound) grant, if any. A STALE grant warns the caller to revalidate.
  const ResourceGrant& grant() const noexcept { return grant_; }

  // Add a requirement. NEVER silently changes need levels. Returns ALLOW or the
  // rejection (e.g. REJECT_IMMUTABLE_CONTRACT) if the change is illegal.
  Outcome add(ResourceRequirement req) noexcept;
  // Replace the whole requirement set in one atomic, generation-advancing step.
  Outcome setRequirements(const std::vector<ResourceRequirement>& reqs) noexcept;

  // Record a grant. The grant must reference the *current* contract generation for
  // it to bind; a grant referencing a stale generation is recorded but STALE.
  Outcome applyGrant(ResourceGrant g) noexcept;
  Outcome releaseGrant(ResourceContractGeneration expecting) noexcept;

  // True if every REQUIRED requirement is satisfied by the current grant.
  bool satisfied() const noexcept;
  // Which REQUIRED requirement is unmet (first unmet), if any.
  std::optional<ResourceRequirement> firstUnsatisfiedRequired() const noexcept;

  // Marks the contract's grant generation advanced (e.g. on coordinator restart).
  Outcome advanceGeneration(ResourceContractGeneration expecting) noexcept;

  void addRequirementForTest(ResourceRequirement req) noexcept { reqs_.push_back(req); }
  void setGrantForTest(ResourceGrant g) noexcept { grant_ = std::move(g); }
  // persistence/reconstruction support: replace requirement set + grant without
  // advancing the generation (the persisted generation is authoritative).
  void setRequirementsNoAdvance(const std::vector<ResourceRequirement>& reqs) noexcept { reqs_ = reqs; }
  void setGenerationNoAdvance(ResourceContractGeneration g) noexcept { generation_ = g; }
  void setIdForTest(ResourceContractId id) noexcept { id_ = id; }

 private:
  ResourceContractId           id_;
  ResourceContractGeneration   generation_;
  std::vector<ResourceRequirement> reqs_;
  ResourceGrant               grant_;
};

}  // namespace wf
