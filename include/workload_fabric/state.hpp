#pragma once
// Explicit lifecycle state machines for the durable workload and the execution
// episode. Transitions are guarded by an explicit, constexpr transition table:
// an illegal transition is rejected by the runtime, and callers can query the
// full set of legal successors to explain a rejection.
#include <cstdint>
#include <string_view>
#include <vector>
#include <array>
#include <utility>

namespace wf {

enum class WorkloadState : std::uint8_t {
  CREATED                  = 0,  // identity registered, not yet validated
  VALIDATING               = 1,  // contract & policy under validation
  BLOCKED                  = 2,  // waiting on some precondition (unspecified)
  WAITING_FOR_DEPENDENCIES = 3,  // gated by required dependencies
  WAITING_FOR_RESOURCES    = 4,  // gated by resource-contract grant
  READY                    = 5,  // all preconditions met, eligible to start
  STARTING                 = 6,  // dispatch requested
  RUNNING                  = 7,  // active execution
  DEGRADED                 = 8,  // running with degraded resources
  SUSPEND_REQUESTED        = 9,  // suspension requested
  SUSPENDING               = 10, // quiescing
  SUSPENDED                = 11, // intentionally not running, resumable
  PREEMPTION_REQUESTED     = 12, // preemption requested
  PREEMPTED                = 13, // preempted, resumable
  MIGRATION_PENDING        = 14, // migration planned
  MIGRATING                = 15, // migration in progress
  RECOVERING               = 16, // recovering after infrastructure loss
  RESTART_PENDING          = 17, // waiting to start a fresh episode
  CANCELLATION_REQUESTED   = 18, // cancellation requested
  CANCELLING               = 19, // turning execution down
  COMPLETING               = 20, // satisfying completion criteria
  COMPLETED                = 21, // terminal success
  FAILED_RECOVERABLE       = 22, // failed, may restart
  FAILED_TERMINAL          = 23, // terminal failure
  CANCELLED                = 24, // terminal cancellation
  SUPERSEDED               = 25, // terminal: superseded by a newer generation
};

enum class EpisodeState : std::uint8_t {
  PENDING   = 0,  // proposed, not admitted
  ADMITTED  = 1,  // admitted, awaiting dispatch
  RUNNING   = 2,
  SUSPENDED = 3,
  PREEMPTED = 4,
  MIGRATING = 5,
  LOST      = 6,  // worker lost / execution ambiguous
  CANCELLED = 7,
  COMPLETED = 8,
  FAILED    = 9,
};

constexpr bool isTerminal(WorkloadState s) noexcept {
  switch (s) {
    case WorkloadState::COMPLETED:
    case WorkloadState::FAILED_TERMINAL:
    case WorkloadState::CANCELLED:
    case WorkloadState::SUPERSEDED:
      return true;
    default:
      return false;
  }
}

constexpr bool isCancelling(WorkloadState s) noexcept {
  return s == WorkloadState::CANCELLATION_REQUESTED || s == WorkloadState::CANCELLING;
}

constexpr bool isActivelyRunning(WorkloadState s) noexcept {
  return s == WorkloadState::RUNNING || s == WorkloadState::DEGRADED || s == WorkloadState::STARTING;
}

constexpr bool isSuspended(WorkloadState s) noexcept {
  return s == WorkloadState::SUSPENDED || s == WorkloadState::PREEMPTED;
}

std::string_view workloadStateName(WorkloadState s) noexcept;
std::string_view episodeStateName(EpisodeState s) noexcept;

// ---- constexpr transition table ---------------------------------------
// Edge list: every (from, to) edge the workload machine permits. Edges are
// deliberately explicit: no wildcards, no implicit shortcuts. A workload can
// only ever take one of these edges, plus policy-gated zero-work completion.
using TransitionEdge = std::pair<WorkloadState, WorkloadState>;
inline constexpr TransitionEdge kTransitionTable[] = {
  {WorkloadState::CREATED, WorkloadState::VALIDATING},
  {WorkloadState::CREATED, WorkloadState::BLOCKED},
  {WorkloadState::CREATED, WorkloadState::WAITING_FOR_DEPENDENCIES},
  {WorkloadState::CREATED, WorkloadState::CANCELLED},   // policy-gated zero-work
  {WorkloadState::CREATED, WorkloadState::COMPLETED},   // policy-gated zero-work

  {WorkloadState::VALIDATING, WorkloadState::CREATED},
  {WorkloadState::VALIDATING, WorkloadState::BLOCKED},
  {WorkloadState::VALIDATING, WorkloadState::WAITING_FOR_DEPENDENCIES},
  {WorkloadState::VALIDATING, WorkloadState::WAITING_FOR_RESOURCES},
  {WorkloadState::VALIDATING, WorkloadState::READY},
  {WorkloadState::VALIDATING, WorkloadState::FAILED_TERMINAL},
  {WorkloadState::VALIDATING, WorkloadState::CANCELLED},

  {WorkloadState::BLOCKED, WorkloadState::VALIDATING},
  {WorkloadState::BLOCKED, WorkloadState::WAITING_FOR_DEPENDENCIES},
  {WorkloadState::BLOCKED, WorkloadState::WAITING_FOR_RESOURCES},
  {WorkloadState::BLOCKED, WorkloadState::READY},
  {WorkloadState::BLOCKED, WorkloadState::CANCELLED},

  {WorkloadState::WAITING_FOR_DEPENDENCIES, WorkloadState::VALIDATING},
  {WorkloadState::WAITING_FOR_DEPENDENCIES, WorkloadState::BLOCKED},
  {WorkloadState::WAITING_FOR_DEPENDENCIES, WorkloadState::WAITING_FOR_RESOURCES},
  {WorkloadState::WAITING_FOR_DEPENDENCIES, WorkloadState::READY},
  {WorkloadState::WAITING_FOR_DEPENDENCIES, WorkloadState::CANCELLED},

  {WorkloadState::WAITING_FOR_RESOURCES, WorkloadState::VALIDATING},
  {WorkloadState::WAITING_FOR_RESOURCES, WorkloadState::BLOCKED},
  {WorkloadState::WAITING_FOR_RESOURCES, WorkloadState::WAITING_FOR_DEPENDENCIES},
  {WorkloadState::WAITING_FOR_RESOURCES, WorkloadState::READY},
  {WorkloadState::WAITING_FOR_RESOURCES, WorkloadState::CANCELLED},

  {WorkloadState::READY, WorkloadState::STARTING},
  {WorkloadState::READY, WorkloadState::WAITING_FOR_RESOURCES},
  {WorkloadState::READY, WorkloadState::WAITING_FOR_DEPENDENCIES},
  {WorkloadState::READY, WorkloadState::CANCELLED},

  {WorkloadState::STARTING, WorkloadState::RUNNING},
  {WorkloadState::STARTING, WorkloadState::READY},          // dispatch failed
  {WorkloadState::STARTING, WorkloadState::RESTART_PENDING},
  {WorkloadState::STARTING, WorkloadState::FAILED_RECOVERABLE},
  {WorkloadState::STARTING, WorkloadState::CANCELLATION_REQUESTED},

  {WorkloadState::RUNNING, WorkloadState::DEGRADED},
  {WorkloadState::RUNNING, WorkloadState::COMPLETING},
  {WorkloadState::RUNNING, WorkloadState::SUSPEND_REQUESTED},
  {WorkloadState::RUNNING, WorkloadState::PREEMPTION_REQUESTED},
  {WorkloadState::RUNNING, WorkloadState::MIGRATION_PENDING},
  {WorkloadState::RUNNING, WorkloadState::CANCELLATION_REQUESTED},
  {WorkloadState::RUNNING, WorkloadState::RECOVERING},
  {WorkloadState::RUNNING, WorkloadState::FAILED_RECOVERABLE},
  {WorkloadState::RUNNING, WorkloadState::FAILED_TERMINAL},

  {WorkloadState::DEGRADED, WorkloadState::RUNNING},
  {WorkloadState::DEGRADED, WorkloadState::COMPLETING},
  {WorkloadState::DEGRADED, WorkloadState::SUSPEND_REQUESTED},
  {WorkloadState::DEGRADED, WorkloadState::PREEMPTION_REQUESTED},
  {WorkloadState::DEGRADED, WorkloadState::MIGRATION_PENDING},
  {WorkloadState::DEGRADED, WorkloadState::CANCELLATION_REQUESTED},
  {WorkloadState::DEGRADED, WorkloadState::RECOVERING},
  {WorkloadState::DEGRADED, WorkloadState::FAILED_RECOVERABLE},

  {WorkloadState::SUSPEND_REQUESTED, WorkloadState::SUSPENDING},
  {WorkloadState::SUSPEND_REQUESTED, WorkloadState::RUNNING},   // suspend aborted
  {WorkloadState::SUSPEND_REQUESTED, WorkloadState::CANCELLATION_REQUESTED},
  {WorkloadState::SUSPEND_REQUESTED, WorkloadState::FAILED_RECOVERABLE},

  {WorkloadState::SUSPENDING, WorkloadState::SUSPENDED},
  {WorkloadState::SUSPENDING, WorkloadState::RUNNING},          // quiesce aborted
  {WorkloadState::SUSPENDING, WorkloadState::CANCELLATION_REQUESTED},
  {WorkloadState::SUSPENDING, WorkloadState::FAILED_RECOVERABLE},

  {WorkloadState::SUSPENDED, WorkloadState::READY},             // resume -> eligible
  {WorkloadState::SUSPENDED, WorkloadState::RUNNING},           // resume into episode
  {WorkloadState::SUSPENDED, WorkloadState::CANCELLATION_REQUESTED},
  {WorkloadState::SUSPENDED, WorkloadState::SUPERSEDED},
  {WorkloadState::SUSPENDED, WorkloadState::FAILED_RECOVERABLE},

  {WorkloadState::PREEMPTION_REQUESTED, WorkloadState::PREEMPTED},
  {WorkloadState::PREEMPTION_REQUESTED, WorkloadState::RUNNING},
  {WorkloadState::PREEMPTION_REQUESTED, WorkloadState::SUSPEND_REQUESTED},
  {WorkloadState::PREEMPTION_REQUESTED, WorkloadState::CANCELLATION_REQUESTED},

  {WorkloadState::PREEMPTED, WorkloadState::RUNNING},
  {WorkloadState::PREEMPTED, WorkloadState::READY},
  {WorkloadState::PREEMPTED, WorkloadState::RESTART_PENDING},
  {WorkloadState::PREEMPTED, WorkloadState::SUPERSEDED},
  {WorkloadState::PREEMPTED, WorkloadState::CANCELLATION_REQUESTED},

  {WorkloadState::MIGRATION_PENDING, WorkloadState::MIGRATING},
  {WorkloadState::MIGRATION_PENDING, WorkloadState::RUNNING},      // migration cancelled
  {WorkloadState::MIGRATION_PENDING, WorkloadState::SUSPEND_REQUESTED},
  {WorkloadState::MIGRATION_PENDING, WorkloadState::CANCELLATION_REQUESTED},
  {WorkloadState::MIGRATION_PENDING, WorkloadState::FAILED_RECOVERABLE},

  {WorkloadState::MIGRATING, WorkloadState::RUNNING},              // committed on target
  {WorkloadState::MIGRATING, WorkloadState::RECOVERING},
  {WorkloadState::MIGRATING, WorkloadState::READY},
  {WorkloadState::MIGRATING, WorkloadState::RESTART_PENDING},
  {WorkloadState::MIGRATING, WorkloadState::SUPERSEDED},
  {WorkloadState::MIGRATING, WorkloadState::CANCELLATION_REQUESTED},

  {WorkloadState::RECOVERING, WorkloadState::RUNNING},
  {WorkloadState::RECOVERING, WorkloadState::RESTART_PENDING},
  {WorkloadState::RECOVERING, WorkloadState::READY},
  {WorkloadState::RECOVERING, WorkloadState::FAILED_RECOVERABLE},
  {WorkloadState::RECOVERING, WorkloadState::FAILED_TERMINAL},
  {WorkloadState::RECOVERING, WorkloadState::CANCELLATION_REQUESTED},

  {WorkloadState::RESTART_PENDING, WorkloadState::READY},
  {WorkloadState::RESTART_PENDING, WorkloadState::RUNNING},
  {WorkloadState::RESTART_PENDING, WorkloadState::FAILED_TERMINAL},  // restart limit exhausted
  {WorkloadState::RESTART_PENDING, WorkloadState::CANCELLATION_REQUESTED},

  {WorkloadState::CANCELLATION_REQUESTED, WorkloadState::CANCELLING},
  {WorkloadState::CANCELLATION_REQUESTED, WorkloadState::CANCELLED},
  {WorkloadState::CANCELLATION_REQUESTED, WorkloadState::COMPLETING}, // completion raced & committed

  {WorkloadState::CANCELLING, WorkloadState::CANCELLED},
  {WorkloadState::CANCELLING, WorkloadState::COMPLETED},             // completion committed before cancel

  {WorkloadState::COMPLETING, WorkloadState::COMPLETED},
  {WorkloadState::COMPLETING, WorkloadState::FAILED_RECOVERABLE},
  {WorkloadState::COMPLETING, WorkloadState::FAILED_TERMINAL},
  {WorkloadState::COMPLETING, WorkloadState::CANCELLED},
};
inline constexpr std::size_t kTransitionCount = sizeof(kTransitionTable) / sizeof(TransitionEdge);

constexpr bool canTransition(WorkloadState from, WorkloadState to) noexcept {
  for (std::size_t i = 0; i < kTransitionCount; ++i) {
    if (kTransitionTable[i].first == from && kTransitionTable[i].second == to) return true;
  }
  return false;
}

// Returns all legal successors of 'from' (explains rejections).
std::vector<WorkloadState> allowedFrom(WorkloadState from);

}  // namespace wf
