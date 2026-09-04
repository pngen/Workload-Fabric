#pragma once
// The durable workload aggregate. A Workload is NOT a process, a worker, an
// attempt, or a node. It is the durable unit of computational intent that
// survives resource grant, restart, migration, suspension, recovery, and
// completion. All authority-bearing mutations are generation-fenced.
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include "workload_fabric/identity.hpp"
#include "workload_fabric/state.hpp"
#include "workload_fabric/resource.hpp"
#include "workload_fabric/dependency.hpp"
#include "workload_fabric/priority.hpp"
#include "workload_fabric/progress.hpp"
#include "workload_fabric/policy.hpp"
#include "workload_fabric/outcome.hpp"
#include "workload_fabric/explain.hpp"
#include "workload_fabric/execution.hpp"
#include "workload_fabric/detail/binary.hpp"

namespace wf {

// A single execution episode. Episodes represent coherent periods in which the
// workload is advanced under one lifecycle incarnation. Each episode may itself
// contain many Execution Fabric *attempts*; episode generation fences authority
// *between* episodes, while attempt generation (owned by Execution Fabric) fences
// authority within one episode.
struct Episode {
  ExecutionEpisodeId          id;
  ExecutionEpisodeGeneration  generation;
  EpisodeState                state = EpisodeState::PENDING;
  OwnerId                     owner;
  OwnerGeneration             ownerGeneration;
  WorkerId                    worker;
  WorkerBootId                boot;
  ExecutionHandle             handle;
  RestartGeneration           restartGeneration;
  RecoveryGeneration          recoveryGeneration;
  CheckpointRef               checkpoint;
  CheckpointGeneration        checkpointGeneration;
  MigrationId                 migrationId;
  MigrationGeneration         migrationGeneration;
  Progress                    progress;
  std::string                 intent;
  bool                        authoritative = true;   // exactly one episode may advance progress
  bool                        failed = false;
  bool                        cancelled = false;
  bool                        completed = false;
  bool                        workerLost = false;
};

// A durable audit entry for the workload's lifecycle.
struct TransitionRecord {
  WorkloadState       from, to;
  WorkloadGeneration  generation;
  std::uint64_t       timestampMs;
  std::string         reason;   // human/machine-readable, deterministic
  Outcome             outcome = Outcome::ALLOW;
};

// The durable Workload aggregate. Not thread-safe by itself: the coordinator (or
// a test harness) owns a mutex around a Workload and serializes mutations.
class Workload {
 public:
  // Create a fresh CREATED workload. The generator supplies a fresh identity.
  static Workload create(WorkloadId id, std::string name, std::string intent);

  WorkloadId               id() const noexcept { return id_; }
  WorkloadGeneration       generation() const noexcept { return generation_; }
  WorkloadRevision         revision() const noexcept { return revision_; }
  WorkloadState            state() const noexcept { return state_; }
  const std::string&       name() const noexcept { return name_; }
  const std::string&       intent() const noexcept { return intent_; }
  const Policy&            policy() const noexcept { return policy_; }
  ResourceContract&        contract() noexcept { return contract_; }
  const ResourceContract&  contract() const noexcept { return contract_; }
  DependencySet&           dependencies() noexcept { return deps_; }
  const DependencySet&     dependencies() const noexcept { return deps_; }
  Priority&                priority() noexcept { return priority_; }
  const Priority&          priority() const noexcept { return priority_; }
  Progress&                progress() noexcept { return progress_; }
  const Progress&          progress() const noexcept { return progress_; }

  OwnerId                  owner() const noexcept { return owner_; }
  OwnerGeneration          ownerGeneration() const noexcept { return ownerGeneration_; }
  CoordinatorEpoch         epoch() const noexcept { return epoch_; }

  uint32_t                 restartCount() const noexcept { return restartCount_; }
  RestartGeneration        restartGeneration() const noexcept { return restartGen_; }
  MigrationId              migrationId() const noexcept { return migrationId_; }
  MigrationGeneration      migrationGeneration() const noexcept { return migrationGen_; }
  CompletionGeneration     completionGeneration() const noexcept { return completionGen_; }
  bool                     cancellationRequested() const noexcept { return cancellationRequested_; }
  bool                     cancelledFinal() const noexcept { return cancelledFinal_; }
  bool                     finalTerminal() const noexcept { return isTerminal(state_); }

  const std::vector<Episode>& episodes() const noexcept { return episodes_; }
  const std::vector<TransitionRecord>& history() const noexcept { return history_; }

  // ---- lifecycle operations (all generation-fenced) ---------------------
  // Transition to 'to' with explicit guard + generation check.
  Outcome transition(WorkloadState to, WorkloadGeneration expecting, const std::string& reason, Explanation* explain = nullptr);

  // Settle into the correct waiting/runnable state based on dependencies + grant.
  Outcome settle(WorkloadGeneration expecting, Explanation* explain = nullptr);

  // Begin a new episode. Advances the workload generation (incarnation) and
  // creates an ADMITTED episode. Rejects if cancelled/terminal.
  Outcome startEpisode(ExecutionEpisodeId id, OwnerId owner, OwnerGeneration ownerGen, bool advanceGeneration, Explanation* explain = nullptr);

  // Admit dispatch to a worker (episode -> RUNNING, attempt associated).
  Outcome admitDispatch(ExecutionEpisodeId episode, WorkerId worker, WorkerBootId boot, Explanation* explain = nullptr);

  // Advance workload progress with episode/authority fencing.
  Outcome reportProgress(ExecutionEpisodeId episode, ExecutionEpisodeGeneration episodeGen, uint64_t delta, bool durable, Explanation* explain = nullptr);

  // Workload-level completion criteria check.
  Outcome commitCompletion(WorkloadGeneration expecting, Explanation* explain = nullptr);

  // A request used by the coordinator to reconcile completion after an episode.
  Outcome requestCancellationWorkloadScope(WorkloadGeneration expecting, Explanation* explain = nullptr);
  Outcome completeCancellation(WorkloadGeneration expecting, Explanation* explain = nullptr);
  Outcome completeSuspend(WorkloadGeneration expecting, Explanation* explain = nullptr);

  // Finalize an explicit finalize completion policy.
  Outcome finalize(WorkloadGeneration expecting, Explanation* explain = nullptr);

  // Mark a worker lost (the authoritative worker for the current episode).
  Outcome onWorkerLost(WorkerId worker, WorkerBootId boot, WorkloadGeneration expecting, Explanation* explain = nullptr);
  Outcome markEpisodeCompleted(ExecutionEpisodeId id, Explanation* explain = nullptr);
  Outcome markEpisodeFailed(ExecutionEpisodeId id, Explanation* explain = nullptr);
  ExecutionEpisodeId currentEpisodeId() const noexcept;

  // Plan + perform an episode restart. Enforces the restart limit.
  Outcome planRestart(WorkloadGeneration expecting, const std::string& reason, Explanation* explain = nullptr);

  // Suspend, resume, migrate primitives.
  Outcome requestSuspend(WorkloadGeneration expecting, Explanation* explain = nullptr);
  Outcome requestResume(WorkloadGeneration expecting, Explanation* explain = nullptr);
  Outcome requestMigration(WorkloadGeneration expecting, Explanation* explain = nullptr);
  Outcome commitMigration(WorkloadGeneration expecting, WorkerId targetWorker, WorkerBootId targetBoot, Explanation* explain = nullptr);

  // ---- persistence codec -----------------------------------------------
  void encode(detail::Writer& w) const;
  static Workload decode(detail::Reader& r, Outcome& status);
  // ---- deterministic explanations --------------------------------------
  // Why is the workload not READY / not runnable / not suspendable / etc.
  Explanation explainBlocked() const;
  Explanation explainRestartAllowed() const;
  Explanation explainMigrationAllowed() const;

  // -------- test/support hooks -------------------------------------------
  void setPolicyForTest(Policy p) noexcept { policy_ = p; }
  void setStateForTest(WorkloadState s) noexcept { state_ = s; }
  void setGenerationForTest(WorkloadGeneration g) noexcept { generation_ = g; }
  void setOwnerForTest(OwnerId o, OwnerGeneration og) noexcept { owner_ = o; ownerGeneration_ = og; }
  void advanceRevision() noexcept { revision_ = next(revision_); }

 private:
  Outcome guardedTransition(WorkloadState to, WorkloadGeneration expecting, const std::string& reason, Explanation* explain);
  Episode* currentEpisode() noexcept;
  Episode const* currentEpisode() const noexcept;
  void record(WorkloadState from, WorkloadState to, const std::string& reason, Outcome outcome);
  bool contractSatisfied() const noexcept { return contract_.satisfied(); }
  bool depsReady() const noexcept { return deps_.ready(); }

  WorkloadId            id_;
  WorkloadGeneration    generation_;
  WorkloadRevision      revision_;
  WorkloadState         state_ = WorkloadState::CREATED;
  std::string           name_;
  std::string           intent_;
  Policy                policy_;
  ResourceContract      contract_;
  DependencySet         deps_;
  Priority              priority_;
  Progress              progress_;
  OwnerId               owner_;
  OwnerGeneration       ownerGeneration_;
  CoordinatorEpoch      epoch_;
  uint32_t              restartCount_ = 0;
  RestartGeneration     restartGen_;
  MigrationId           migrationId_;
  MigrationGeneration   migrationGen_;
  CompletionGeneration  completionGen_;
  bool                  cancellationRequested_ = false;
  bool                  cancelledFinal_ = false;
  bool                  cancellationWasRejected_ = false;
  std::string           lastRestartReason_;
  ExecutionEpisodeId    lastSourceEpisode_;
  std::uint64_t         episodeCounterForMigration_ = 2000;
  std::vector<Episode>  episodes_;
  std::vector<TransitionRecord> history_;
};

}  // namespace wf
