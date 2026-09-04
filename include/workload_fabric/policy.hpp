#pragma once
// Lifecycle policy knobs: restart, completion, migration, suspension, and
// cancellation semantics. Combinations are validated; policies are generation-fenced.
#include <cstdint>
#include <chrono>
#include <string_view>
#include <optional>
#include "workload_fabric/identity.hpp"

namespace wf {

enum class RestartKind : std::uint8_t {
  NEVER = 0,
  ON_KNOWN_FAILURE = 1,
  ON_WORKER_LOSS = 2,
  ON_INFRASTRUCTURE_FAILURE = 3,
  ON_AMBIGUOUS_EXECUTION = 4,
  ALWAYS_WITH_LIMIT = 5,
  MANUAL_ONLY = 6,
};
std::string_view restartKindName(RestartKind k) noexcept;

enum class CompletionKind : std::uint8_t {
  SINGLE_RESULT = 0,
  ALL_PHASES = 1,
  REQUIRED_UNITS = 2,
  EXPLICIT_FINALIZE = 3,
};
std::string_view completionKindName(CompletionKind k) noexcept;

enum class MigrationEligibility : std::uint8_t { NONE = 0, PLAN_ONLY = 1, CHECKPOINT_AND_RESUME = 2 };
std::string_view migrationEligibilityName(MigrationEligibility e) noexcept;

// Deterministic COMPLETE-vs-CANCEL race semantics. Once one side commits, the
// other is rejected; there is never a double terminal transition.
enum class CancelRaceSemantics : std::uint8_t {
  CANCEL_FIRST = 0,   // if cancellation is requested before completion commits, cancel wins
  COMPLETION_FIRST = 1, // completion that is already committed wins over concurrent cancel
};
std::string_view cancelRaceSemanticsName(CancelRaceSemantics s) noexcept;

struct RestartPolicy {
  RestartKind kind = RestartKind::NEVER;
  std::uint32_t maxRestarts = 0;                       // applies for ALWAYS_WITH_LIMIT
  std::uint32_t maxRecoverySeconds = 0;                // 0 == unbounded
  bool carryProgressForward = true;
  bool carryCheckpoint = true;
};

struct CompletionPolicy {
  CompletionKind kind = CompletionKind::SINGLE_RESULT;
  std::uint32_t requiredUnits = 0;                     // for REQUIRED_UNITS
  bool requireDurableArtifact = true;
  bool allowZeroWorkCompletion = false;                // CREATED -> COMPLETED shortcut
};

struct MigrationPolicy {
  MigrationEligibility eligibility = MigrationEligibility::NONE;
  bool requireCheckpoint = true;
  bool requireQuiescence = true;
  bool allowLossy = false;                             // drop progress on migration
};

struct SuspensionPolicy {
  bool allowed = true;
  bool requireDurableState = true;
  std::uint32_t maxSuspendedSeconds = 0;               // 0 == indefinite lease
  bool releaseResourcesWhileSuspended = true;
};

struct Policy {
  RestartPolicy     restart;
  CompletionPolicy  completion;
  MigrationPolicy   migration;
  SuspensionPolicy  suspension;
  CancelRaceSemantics cancelRace = CancelRaceSemantics::COMPLETION_FIRST;
  PolicyGeneration  generation;                       // fenced; advances on any policy edit
};

// Restart-limit predicate: false once count >= maxRestarts, i.e. the (count+1)th
// restart is forbidden. Provably enforces 'max_restarts=N must not restart N+1 times'.
constexpr bool restartAllowed(std::uint32_t count, std::uint32_t maxRestarts) noexcept {
  return count < maxRestarts;
}

}  // namespace wf
