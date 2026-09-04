#pragma once
// Narrow Execution Fabric integration. Workload Fabric never re-implements the
// execution-attempt authority; it drives it through this interface. Tests may use
// the bundled reference adapter (see src/execution/reference_execution.cpp).
#include <cstdint>
#include <string>
#include <optional>
#include "workload_fabric/outcome.hpp"
#include "workload_fabric/identity.hpp"
#include "workload_fabric/state.hpp"

namespace wf {

// An opaque logical execution handle owned by Execution Fabric. Workload Fabric
// references it but does not redefine execution authority.
struct ExecutionHandle {
  WorkerId      worker;
  WorkerBootId  boot;
  std::uint64_t attemptGeneration = 0;
  std::string   intent;   // opaque token naming the dispatched unit of work
};

// The observable state of a dispatched execution, as reported by Execution Fabric.
enum class ExecutionState : std::uint8_t {
  DISPATCHED = 0, RUNNING = 1, SUSPENDED = 2, PREEMPTED = 3, CANCELLED = 4,
  COMPLETED = 5, FAILED = 6, LOST = 7, UNKNOWN = 8,
};
std::string_view executionStateName(ExecutionState s) noexcept;

// The narrow, synchronous contract with Execution Fabric.
class ExecutionFabric {
 public:
  virtual ~ExecutionFabric() = default;

  // Ask Execution Fabric to begin work for an episode. Returns a handle, or an
  // outcome saying why not (e.g. no worker available -> DEFER).
  virtual Outcome requestExecution(const ExecutionEpisodeId& episode, const std::string& intent) = 0;

  // Observe the current authoritative execution state for a handle.
  virtual Outcome observeExecution(const ExecutionHandle& handle, ExecutionState& out) = 0;

  // Cancel an execution (best effort propagation).
  virtual Outcome cancelExecution(const ExecutionHandle& handle) = 0;

  // Preempt / resume at the execution-attempt boundary.
  virtual Outcome preemptExecution(const ExecutionHandle& handle) = 0;
  virtual Outcome resumeExecution(const ExecutionHandle& handle) = 0;

  // Authoritative completion notice. out_authoritative says whether this is the
  // single authoritative completion for the execution lineage (fenced by attempt gen).
  virtual Outcome observeCompletion(const ExecutionHandle& handle, bool& out_authoritative) = 0;

  // The handle currently authoritative for an episode, if any.
  virtual std::optional<ExecutionHandle> currentHandle(const ExecutionEpisodeId& episode) = 0;
};

}  // namespace wf
