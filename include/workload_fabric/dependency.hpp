#pragma once
// Workload-facing dependency model. Dependency Fabric will own the deeper DAG;
// Workload Fabric needs only enough to answer: *what must be true before this
// workload may advance?* Dependencies are generation-fenced.
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <string_view>
#include "workload_fabric/identity.hpp"
#include "workload_fabric/outcome.hpp"

namespace wf {

enum class DependencyRequirement : std::uint8_t { REQUIRED = 0, OPTIONAL = 1 };
std::string_view dependencyRequirementName(DependencyRequirement r) noexcept;

// What must hold about the depended-on workload for this dependency to be ready.
enum class DependencyReady : std::uint8_t {
  COMPLETED = 0,   // the dependency must reach a terminal success
  READY = 1,       // the dependency must be READY
  ARTIFACT_READY = 2, // the dependency's durable artifact/state must be available
};
std::string_view dependencyReadyName(DependencyReady r) noexcept;

// The durable definition of one dependency edge (the dependee).
struct DependencyDef {
  WorkloadId             dependsOn;
  DependencyRequirement  requirement = DependencyRequirement::REQUIRED;
  DependencyReady        readyWhen = DependencyReady::COMPLETED;
  std::string            artifactKey;   // for ARTIFACT_READY deps
};

// The live state of one dependency edge for the current set generation.
enum class DependencyStatus : std::uint8_t {
  PENDING = 0, SATISFIED = 1, FAILED = 2, INVALIDATED = 3, STALE = 4,
};
std::string_view dependencyStatusName(DependencyStatus s) noexcept;

struct DependencyState {
  DependencyStatus    status = DependencyStatus::PENDING;
  DependencyGeneration observedGeneration;   // generation of the dependee when observed
  std::string         detail;
};

// A versioned dependency set. The set identity is immutable; each mutation
// advances DependencyGeneration. Stale generations cannot unblock a workload.
class DependencySet {
 public:
  DependencySet() = default;
  DependencySet(DependencySetId id, DependencyGeneration gen) : id_(id), gen_(gen) {}

  DependencySetId        id() const noexcept { return id_; }
  DependencyGeneration   generation() const noexcept { return gen_; }
  const std::vector<DependencyDef>& defs() const noexcept { return defs_; }

  Outcome define(const DependencyDef& d) noexcept;
  Outcome clear() noexcept;
  Outcome setStatus(std::size_t index, DependencyStatus status, DependencyGeneration observed) noexcept;

  // Gating predicate: true iff every REQUIRED dependency is currently SATISFIED for
  // the current set generation. OPTIONAL dependencies never block readiness; and an
  // optional dependency failing is non-terminal unless policy says otherwise.
  bool ready() const noexcept;
  std::optional<std::size_t> firstUnmetRequired() const noexcept;

  const std::vector<DependencyState>& states() const noexcept { return states_; }
  void addDefForTest(const DependencyDef& d) noexcept { defs_.push_back(d); states_.emplace_back(); }
  void setStateForTest(std::size_t i, DependencyState s) noexcept { states_.at(i) = std::move(s); }
  void setIdForTest(DependencySetId id) noexcept { id_ = id; }
  void setGenerationForTest(DependencyGeneration g) noexcept { gen_ = g; }

 private:
  DependencySetId           id_;
  DependencyGeneration      gen_;
  std::vector<DependencyDef> defs_;
  std::vector<DependencyState> states_;
};

}  // namespace wf
