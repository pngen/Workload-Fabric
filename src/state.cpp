#include "workload_fabric/state.hpp"
#include "workload_fabric/outcome.hpp"

namespace wf {

namespace {
inline constexpr const char* kWs[] = {
  "CREATED",
  "VALIDATING",
  "BLOCKED",
  "WAITING_FOR_DEPENDENCIES",
  "WAITING_FOR_RESOURCES",
  "READY",
  "STARTING",
  "RUNNING",
  "DEGRADED",
  "SUSPEND_REQUESTED",
  "SUSPENDING",
  "SUSPENDED",
  "PREEMPTION_REQUESTED",
  "PREEMPTED",
  "MIGRATION_PENDING",
  "MIGRATING",
  "RECOVERING",
  "RESTART_PENDING",
  "CANCELLATION_REQUESTED",
  "CANCELLING",
  "COMPLETING",
  "COMPLETED",
  "FAILED_RECOVERABLE",
  "FAILED_TERMINAL",
  "CANCELLED",
  "SUPERSEDED",
};
inline constexpr const char* kEs[] = {
  "PENDING",
  "ADMITTED",
  "RUNNING",
  "SUSPENDED",
  "PREEMPTED",
  "MIGRATING",
  "LOST",
  "CANCELLED",
  "COMPLETED",
  "FAILED",
};
}  // namespace

std::string_view workloadStateName(WorkloadState s) noexcept {
  auto i = static_cast<std::size_t>(s);
  if (i >= sizeof(kWs) / sizeof(kWs[0])) return "INVALID";
  return kWs[i];
}

std::string_view episodeStateName(EpisodeState s) noexcept {
  auto i = static_cast<std::size_t>(s);
  if (i >= sizeof(kEs) / sizeof(kEs[0])) return "INVALID";
  return kEs[i];
}

std::vector<WorkloadState> allowedFrom(WorkloadState from) {
  std::vector<WorkloadState> out;
  for (std::size_t i = 0; i < kTransitionCount; ++i) {
    if (kTransitionTable[i].first == from) out.push_back(kTransitionTable[i].second);
  }
  return out;
}

}  // namespace wf
