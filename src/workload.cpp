#include "workload_fabric/workload.hpp"
#include <algorithm>
#include <sstream>

namespace wf {

namespace {
}  // namespace

Workload Workload::create(WorkloadId id, std::string name, std::string intent) {
  Workload w;
  w.id_ = id;
  w.name_ = std::move(name);
  w.intent_ = std::move(intent);
  w.generation_ = WorkloadGeneration::fromU64(1);
  w.revision_ = WorkloadRevision::fromU64(1);
  w.epoch_ = CoordinatorEpoch::fromU64(1);
  w.state_ = WorkloadState::CREATED;
  w.policy_.generation = PolicyGeneration::fromU64(1);
  return w;
}

void Workload::record(WorkloadState from, WorkloadState to, const std::string& reason, Outcome outcome) {
  TransitionRecord r;
  r.from = from; r.to = to;
  r.generation = generation_;
  r.timestampMs = 0;
  r.reason = reason;
  r.outcome = outcome;
  history_.push_back(std::move(r));
  revision_ = next(revision_);
}

Episode* Workload::currentEpisode() noexcept {
  for (auto& e : episodes_) { if (e.authoritative) return &e; }
  return episodes_.empty() ? nullptr : &episodes_.back();
}

Episode const* Workload::currentEpisode() const noexcept {
  for (auto& e : episodes_) { if (e.authoritative) return &e; }
  return episodes_.empty() ? nullptr : &episodes_.back();
}

Outcome Workload::guardedTransition(WorkloadState to, WorkloadGeneration expecting, const std::string& reason, Explanation* explain) {
  if (expecting != generation_) {
    if (explain) { explain->outcome = Outcome::REJECT_STALE_WORKLOAD_GENERATION; explain->from = state_; explain->to = to; explain->workloadGeneration = generation_; explain->detail = "stale generation " + std::to_string(expecting.toU64()) + " (current " + std::to_string(generation_.toU64()) + ")"; }
    record(state_, to, reason, Outcome::REJECT_STALE_WORKLOAD_GENERATION);
    return Outcome::REJECT_STALE_WORKLOAD_GENERATION;
  }
  if (state_ == to) { if (explain) { explain->outcome = Outcome::ALLOW; explain->from = state_; explain->to = to; explain->workloadGeneration = generation_; } return Outcome::ALLOW; }
  if (isTerminal(state_)) {
    if (explain) { explain->outcome = Outcome::REJECT_TERMINAL; explain->from = state_; explain->to = to; explain->detail = "terminal state " + std::string(workloadStateName(state_)) + " cannot reopen"; }
    record(state_, to, reason, Outcome::REJECT_TERMINAL);
    return Outcome::REJECT_TERMINAL;
  }
  if (!canTransition(state_, to)) {
    if (explain) { explain->outcome = Outcome::REJECT_INVALID_TRANSITION; explain->from = state_; explain->to = to; explain->workloadGeneration = generation_; explain->detail = "no edge " + std::string(workloadStateName(state_)) + " -> " + std::string(workloadStateName(to)); }
    record(state_, to, reason, Outcome::REJECT_INVALID_TRANSITION);
    return Outcome::REJECT_INVALID_TRANSITION;
  }
  // Zero-work completion gate: CREATED -> COMPLETED without any execution/proof
  // must reject unless the workload type explicitly allows zero-work completion.
  if (to == WorkloadState::COMPLETED && !policy_.completion.allowZeroWorkCompletion) {
    if (state_ == WorkloadState::CREATED || state_ == WorkloadState::VALIDATING || state_ == WorkloadState::BLOCKED ||
        state_ == WorkloadState::WAITING_FOR_DEPENDENCIES || state_ == WorkloadState::WAITING_FOR_RESOURCES) {
      if (explain) { explain->outcome = Outcome::REJECT_COMPLETION_POLICY; explain->from = state_; explain->to = to; explain->detail = "zero-work completion not allowed by policy"; }
      record(state_, to, reason, Outcome::REJECT_COMPLETION_POLICY);
      return Outcome::REJECT_COMPLETION_POLICY;
    }
  }
  WorkloadState from = state_;
  state_ = to;
  record(from, to, reason, Outcome::ALLOW);
  if (explain) { explain->outcome = Outcome::ALLOW; explain->from = from; explain->to = to; explain->workloadGeneration = generation_; explain->detail = reason; }
  return Outcome::ALLOW;
}

Outcome Workload::transition(WorkloadState to, WorkloadGeneration expecting, const std::string& reason, Explanation* explain) {
  return guardedTransition(to, expecting, reason, explain);
}

Outcome Workload::settle(WorkloadGeneration expecting, Explanation* explain) {
  if (expecting != generation_) return Outcome::REJECT_STALE_WORKLOAD_GENERATION;
  WorkloadState target;
  if (!depsReady()) {
    if (isTerminal(state_) || isCancelling(state_)) return Outcome::REJECT_TERMINAL;
    target = WorkloadState::WAITING_FOR_DEPENDENCIES;
  } else if (!contractSatisfied()) {
    if (isTerminal(state_) || isCancelling(state_)) return Outcome::REJECT_TERMINAL;
    target = WorkloadState::WAITING_FOR_RESOURCES;
  } else {
    target = WorkloadState::READY;
  }
  if (state_ == target) return Outcome::ALLOW;
  return guardedTransition(target, expecting, "settle", explain);
}

Outcome Workload::startEpisode(ExecutionEpisodeId id, OwnerId owner, OwnerGeneration ownerGen, bool advanceGeneration, Explanation* explain) {
  // Cancellation at workload scope must stop new episodes outright.
  if (cancelledFinal_ || isCancelling(state_)) {
    if (explain) { explain->outcome = Outcome::REJECT_CANCELLED; explain->from = state_; explain->detail = "workload cancelled; no new episodes"; }
    return Outcome::REJECT_CANCELLED;
  }
  if (isTerminal(state_)) return Outcome::REJECT_TERMINAL;
  const bool resumeEligible = state_ == WorkloadState::READY || state_ == WorkloadState::RESTART_PENDING ||
      state_ == WorkloadState::SUSPENDED || state_ == WorkloadState::PREEMPTED || state_ == WorkloadState::RECOVERING;
  if (!resumeEligible) {
    if (explain) { explain->outcome = Outcome::REJECT_EPISODE_INELIGIBLE; explain->from = state_; explain->detail = "cannot start episode from " + std::string(workloadStateName(state_)); }
    return Outcome::REJECT_EPISODE_INELIGIBLE;
  }
  // A non-READY resume (restart / resume / recover) must first become READY so the
  // transition table stays explicit; only then may an episode start.
  if (state_ != WorkloadState::READY) {
    Outcome pre = guardedTransition(WorkloadState::READY, generation_, "resume eligible", explain);
    if (!isAllow(pre)) return pre;
  }
  Outcome t = guardedTransition(WorkloadState::STARTING, generation_, "start episode", explain);
  if (!isAllow(t)) return t;

  // The new episode takes over authoritative progress advancement.
  for (auto& e : episodes_) e.authoritative = false;
  Episode ep;
  ep.id = id;
  ep.generation = ExecutionEpisodeGeneration::fromU64(1);
  ep.state = EpisodeState::ADMITTED;
  ep.owner = owner;
  ep.ownerGeneration = ownerGen;
  ep.intent = intent_;
  ep.authoritative = true;
  ep.progress = progress_;  // carry forward known durable progress
  episodes_.push_back(std::move(ep));

  if (advanceGeneration) generation_ = next(generation_);
  return Outcome::ALLOW;
}

Outcome Workload::admitDispatch(ExecutionEpisodeId episode, WorkerId worker, WorkerBootId boot, Explanation* explain) {
  Episode* ep = nullptr;
  for (auto& e : episodes_) { if (e.id == episode) { ep = &e; break; } }
  if (!ep) return Outcome::REJECT_EPISODE_INELIGIBLE;
  if (ep->state != EpisodeState::ADMITTED && ep->state != EpisodeState::PREEMPTED) return Outcome::REJECT_EPISODE_INELIGIBLE;
  ep->worker = worker;
  ep->boot = boot;
  ep->state = EpisodeState::RUNNING;
  if (state_ == WorkloadState::STARTING) return guardedTransition(WorkloadState::RUNNING, generation_, "dispatch", explain);
  return Outcome::ALLOW;
}

Outcome Workload::reportProgress(ExecutionEpisodeId episode, ExecutionEpisodeGeneration episodeGen, uint64_t delta, bool durable, Explanation* explain) {
  Episode* ep = nullptr;
  for (auto& e : episodes_) { if (e.id == episode) { ep = &e; break; } }
  if (!ep) return Outcome::REJECT_EPISODE_INELIGIBLE;
  if (episodeGen != ep->generation) { if (explain) { explain->outcome = Outcome::REJECT_STALE_EPISODE; explain->detail = "stale episode generation"; } return Outcome::REJECT_STALE_EPISODE; }
  if (!ep->authoritative) { if (explain) { explain->outcome = Outcome::REJECT_EPISODE_INELIGIBLE; explain->detail = "episode not authoritative"; } return Outcome::REJECT_EPISODE_INELIGIBLE; }
  if (cancelledFinal_ || isCancelling(state_)) return Outcome::REJECT_CANCELLED;
  if (isTerminal(state_)) return Outcome::REJECT_TERMINAL;
  // A double-count or over-commit past a KNOWN total is rejected, never trusted.
  if (progress_.totalKind == TotalKind::KNOWN && progress_.completed + delta > progress_.total) return Outcome::REJECT_PROGRESS_DOUBLE_COUNT;
  if (!progress_.advance(delta, durable)) return Outcome::REJECT_UNKNOWN;
  ep->progress.completed += delta;
  if (durable) ep->progress.checkpointed = ep->progress.completed;
  return Outcome::ALLOW;
}

Outcome Workload::commitCompletion(WorkloadGeneration expecting, Explanation* explain) {
  if (expecting != generation_) return Outcome::REJECT_STALE_WORKLOAD_GENERATION;
  if (isTerminal(state_)) return Outcome::REJECT_TERMINAL;
  if (cancellationRequested_ && policy_.cancelRace == CancelRaceSemantics::CANCEL_FIRST) {
    if (explain) { explain->outcome = Outcome::REJECT_CANCELLED; explain->detail = "cancellation requested before completion; CANCEL_FIRST"; }
    return Outcome::REJECT_CANCELLED;
  }

  Episode* cur = currentEpisode();
  bool criterion = false;
  switch (policy_.completion.kind) {
    case CompletionKind::SINGLE_RESULT:
      criterion = cur != nullptr && cur->completed;
      break;
    case CompletionKind::ALL_PHASES:
      criterion = cur != nullptr && cur->completed && progress_.complete();
      break;
    case CompletionKind::REQUIRED_UNITS:
      criterion = progress_.totalKind == TotalKind::KNOWN && progress_.completed >= progress_.total;
      break;
    case CompletionKind::EXPLICIT_FINALIZE:
      criterion = false;  // only finalize() satisfies this policy
      break;
  }
  if (!criterion) { if (explain) { explain->outcome = Outcome::REJECT_COMPLETION_POLICY; explain->detail = "completion criteria not met"; } return Outcome::REJECT_COMPLETION_POLICY; }

  if (cur) cur->completed = true;
  // Complete the race: a legitimately satisfied completion wins and rejects the cancel.
  cancellationRequested_ = false;
  cancellationWasRejected_ = true;
  completionGen_ = next(completionGen_);
  Outcome a = guardedTransition(WorkloadState::COMPLETING, expecting, "completion criteria met", explain);
  if (!isAllow(a)) return a;
  Outcome b = guardedTransition(WorkloadState::COMPLETED, generation_, "workload completed", explain);
  return b;
}

Outcome Workload::finalize(WorkloadGeneration expecting, Explanation* explain) {
  if (expecting != generation_) return Outcome::REJECT_STALE_WORKLOAD_GENERATION;
  if (policy_.completion.kind != CompletionKind::EXPLICIT_FINALIZE) return Outcome::REJECT_COMPLETION_POLICY;
  if (isTerminal(state_)) return Outcome::REJECT_TERMINAL;
  completionGen_ = next(completionGen_);
  Outcome a = guardedTransition(WorkloadState::COMPLETING, expecting, "explicit finalize", explain);
  if (!isAllow(a)) return a;
  return guardedTransition(WorkloadState::COMPLETED, generation_, "finalized", explain);
}

Outcome Workload::requestCancellationWorkloadScope(WorkloadGeneration expecting, Explanation* explain) {
  if (expecting != generation_) return Outcome::REJECT_STALE_WORKLOAD_GENERATION;
  if (isTerminal(state_)) return Outcome::REJECT_TERMINAL;
  cancellationRequested_ = true;
  if (state_ == WorkloadState::CANCELLATION_REQUESTED || state_ == WorkloadState::CANCELLING) return Outcome::ALLOW;
  if (state_ == WorkloadState::COMPLETED || state_ == WorkloadState::CANCELLED || state_ == WorkloadState::FAILED_TERMINAL || state_ == WorkloadState::SUPERSEDED) return Outcome::REJECT_TERMINAL;
  return guardedTransition(WorkloadState::CANCELLATION_REQUESTED, expecting, "cancellation requested", explain);
}

Outcome Workload::completeCancellation(WorkloadGeneration expecting, Explanation* explain) {
  if (expecting != generation_) return Outcome::REJECT_STALE_WORKLOAD_GENERATION;
  if (state_ == WorkloadState::CANCELLED) { cancelledFinal_ = true; return Outcome::ALLOW; }
  if (isTerminal(state_)) return Outcome::REJECT_TERMINAL;
  cancelledFinal_ = true;
  cancellationRequested_ = true;
  Outcome a = guardedTransition(WorkloadState::CANCELLATION_REQUESTED, expecting, "cancelling", explain);
  if (!isAllow(a)) return a;
  Outcome b = guardedTransition(WorkloadState::CANCELLING, generation_, "cancelling", explain);
  if (!isAllow(b)) return b;
  return guardedTransition(WorkloadState::CANCELLED, generation_, "cancelled", explain);
}

Outcome Workload::onWorkerLost(WorkerId worker, WorkerBootId boot, WorkloadGeneration expecting, Explanation* explain) {
  if (expecting != generation_) return Outcome::REJECT_STALE_WORKLOAD_GENERATION;
  Episode* ep = currentEpisode();
  if (!ep) return Outcome::REJECT_EPISODE_INELIGIBLE;
  // A fresh WorkerBootId must never regain authority: only the currently-recorded
  // boot is lost, and a stale boot report is rejected outright.
  if (ep->worker != worker || ep->boot != boot) { if (explain) { explain->outcome = Outcome::REJECT_STALE_BOOT; explain->detail = "worker/boot mismatch for current episode"; } return Outcome::REJECT_STALE_BOOT; }
  ep->workerLost = true;
  ep->state = EpisodeState::LOST;
  if (state_ == WorkloadState::RUNNING || state_ == WorkloadState::DEGRADED) return guardedTransition(WorkloadState::RECOVERING, expecting, "worker lost", explain);
  return Outcome::ALLOW;
}

Outcome Workload::markEpisodeCompleted(ExecutionEpisodeId id, Explanation* explain) {
  for (auto& e : episodes_) if (e.id == id) { e.completed = true; e.state = EpisodeState::COMPLETED; return Outcome::ALLOW; }
  if (explain) explain->outcome = Outcome::REJECT_EPISODE_INELIGIBLE;
  return Outcome::REJECT_EPISODE_INELIGIBLE;
}
Outcome Workload::markEpisodeFailed(ExecutionEpisodeId id, Explanation* explain) {
  for (auto& e : episodes_) if (e.id == id) { e.failed = true; e.state = EpisodeState::FAILED; return Outcome::ALLOW; }
  if (explain) explain->outcome = Outcome::REJECT_EPISODE_INELIGIBLE;
  return Outcome::REJECT_EPISODE_INELIGIBLE;
}
ExecutionEpisodeId Workload::currentEpisodeId() const noexcept {
  if (auto* e = const_cast<Workload*>(this)->currentEpisode()) return e->id;
  return {};
}

Outcome Workload::planRestart(WorkloadGeneration expecting, const std::string& reason, Explanation* explain) {
  if (expecting != generation_) return Outcome::REJECT_STALE_WORKLOAD_GENERATION;
  if (isTerminal(state_)) { if (explain) { explain->outcome = Outcome::REJECT_TERMINAL; explain->from = state_; explain->detail = "terminal workload cannot restart"; } return Outcome::REJECT_TERMINAL; }
  const RestartPolicy& rp = policy_.restart;
  // Whether the trigger class is allowed by the restart kind.
  bool classAllowed = false;
  switch (rp.kind) {
    case RestartKind::ALWAYS_WITH_LIMIT: classAllowed = true; break;
    case RestartKind::ON_KNOWN_FAILURE: classAllowed = reason != "WORKER_LOSS"; break;
    case RestartKind::ON_WORKER_LOSS: classAllowed = (reason == "WORKER_LOSS"); break;
    case RestartKind::ON_INFRASTRUCTURE_FAILURE: classAllowed = (reason == "INFRASTRUCTURE_LOSS" || reason == "WORKER_LOSS"); break;
    case RestartKind::ON_AMBIGUOUS_EXECUTION: classAllowed = (reason == "AMBIGUOUS_EXECUTION"); break;
    case RestartKind::NEVER: classAllowed = false; break;
    case RestartKind::MANUAL_ONLY: classAllowed = false; break;
  }
  if (!classAllowed || !restartAllowed(restartCount_, rp.maxRestarts)) {
    if (explain) { explain->outcome = Outcome::REJECT_RESTART_LIMIT; explain->from = state_; explain->detail = "restart not allowed (count=" + std::to_string(restartCount_) + " max=" + std::to_string(rp.maxRestarts) + " kind=" + std::string(restartKindName(rp.kind)) + ")"; }
    return Outcome::REJECT_RESTART_LIMIT;
  }
  restartCount_++;
  restartGen_ = next(restartGen_);
  lastRestartReason_ = reason;
  if (Episode* e = currentEpisode()) lastSourceEpisode_ = e->id;
  // release/settle at the workload level.
  if (state_ == WorkloadState::RESTART_PENDING) return Outcome::ALLOW;
  return guardedTransition(WorkloadState::RESTART_PENDING, expecting, "restart pending", explain);
}

Outcome Workload::requestSuspend(WorkloadGeneration expecting, Explanation* explain) {
  if (expecting != generation_) return Outcome::REJECT_STALE_WORKLOAD_GENERATION;
  if (!policy_.suspension.allowed) return Outcome::REJECT_DEGRADED_POLICY;
  if (!isActivelyRunning(state_)) { if (explain) { explain->outcome = Outcome::REJECT_NOT_SUSPENDED; explain->detail = "not in a suspendable state"; } return Outcome::REJECT_NOT_SUSPENDED; }
  if (state_ == WorkloadState::SUSPEND_REQUESTED) return Outcome::ALLOW;
  return guardedTransition(WorkloadState::SUSPEND_REQUESTED, expecting, "suspend requested", explain);
}

Outcome Workload::completeSuspend(WorkloadGeneration expecting, Explanation* explain) {
  if (expecting != generation_) return Outcome::REJECT_STALE_WORKLOAD_GENERATION;
  if (state_ == WorkloadState::SUSPENDED) return Outcome::ALLOW;
  if (state_ != WorkloadState::SUSPEND_REQUESTED && state_ != WorkloadState::SUSPENDING) return Outcome::REJECT_NOT_RESUMABLE;
  if (Episode* e = currentEpisode()) { e->state = EpisodeState::SUSPENDED; e->workerLost = false; }
  if (state_ == WorkloadState::SUSPEND_REQUESTED) {
    Outcome a = guardedTransition(WorkloadState::SUSPENDING, expecting, "suspending", explain);
    if (!isAllow(a)) return a;
    return guardedTransition(WorkloadState::SUSPENDED, generation_, "suspended", explain);
  }
  return guardedTransition(WorkloadState::SUSPENDED, expecting, "suspended", explain);
}

Outcome Workload::requestResume(WorkloadGeneration expecting, Explanation* explain) {
  if (expecting != generation_) return Outcome::REJECT_STALE_WORKLOAD_GENERATION;
  if (state_ != WorkloadState::SUSPENDED && state_ != WorkloadState::PREEMPTED) {
    if (explain) { explain->outcome = Outcome::REJECT_NOT_SUSPENDED; explain->detail = "workload is not suspended or preempted"; }
    return Outcome::REJECT_NOT_SUSPENDED;
  }
  return guardedTransition(WorkloadState::READY, expecting, "resume eligible", explain);
}

Outcome Workload::requestMigration(WorkloadGeneration expecting, Explanation* explain) {
  if (expecting != generation_) return Outcome::REJECT_STALE_WORKLOAD_GENERATION;
  if (policy_.migration.eligibility == MigrationEligibility::NONE) {
    if (explain) { explain->outcome = Outcome::REJECT_MIGRATION_INELIGIBLE; explain->detail = "migration not eligible per policy"; }
    return Outcome::REJECT_MIGRATION_INELIGIBLE;
  }
  if (!isActivelyRunning(state_) && state_ != WorkloadState::MIGRATION_PENDING) return Outcome::REJECT_MIGRATION_INELIGIBLE;
  migrationId_ = MigrationId::fromU64(migrationId_.toU64() + 1);
  migrationGen_ = next(migrationGen_);
  if (state_ == WorkloadState::MIGRATION_PENDING) return Outcome::ALLOW;
  if (state_ == WorkloadState::MIGRATING) return Outcome::ALLOW;
  return guardedTransition(WorkloadState::MIGRATION_PENDING, expecting, "migration requested", explain);
}

Outcome Workload::commitMigration(WorkloadGeneration expecting, WorkerId targetWorker, WorkerBootId targetBoot, Explanation* explain) {
  if (expecting != generation_) return Outcome::REJECT_STALE_WORKLOAD_GENERATION;
  if (state_ != WorkloadState::MIGRATION_PENDING && state_ != WorkloadState::MIGRATING) return Outcome::REJECT_MIGRATION_INELIGIBLE;
  if (state_ == WorkloadState::MIGRATION_PENDING) {
    Outcome a = guardedTransition(WorkloadState::MIGRATING, expecting, "migrating", explain);
    if (!isAllow(a)) return a;
  }
  // Quiesce source: exactly one lineage may advance authoritative progress. The
  // source episode loses authority; a NEW target episode becomes authoritative.
  if (!episodes_.empty()) for (auto& e : episodes_) e.authoritative = false;
  Episode target;
  target.id = ExecutionEpisodeId::fromU64(episodeCounterForMigration_++);
  target.generation = ExecutionEpisodeGeneration::fromU64(1);
  target.state = EpisodeState::RUNNING;
  target.worker = targetWorker;
  target.boot = targetBoot;
  target.owner = owner_;
  target.ownerGeneration = ownerGeneration_;
  target.intent = intent_;
  target.authoritative = true;
  target.progress = progress_;  // carry durable progress
  episodes_.push_back(std::move(target));
  generation_ = next(generation_);  // migration is an authoritative incarnation boundary
  return guardedTransition(WorkloadState::RUNNING, generation_, "migration committed", explain);
}

Explanation Workload::explainBlocked() const {
  Explanation e;
  if (!depsReady()) { e.outcome = Outcome::BLOCKED_DEPENDENCY; e.from = state_; if (auto i = deps_.firstUnmetRequired()) { e.detail = "required dependency[" + std::to_string(*i) + "] not satisfied"; } else { e.detail = "required dependencies not satisfied"; } }
  else if (!contractSatisfied()) { e.outcome = Outcome::BLOCKED_RESOURCE_CONTRACT; e.from = state_; if (auto r = contract_.firstUnsatisfiedRequired()) { e.detail = "unmet resource " + std::string(resourceKindName(r->kind)); } else { e.detail = "resource contract not satisfied"; } }
  else { e.outcome = Outcome::ALLOW; e.from = state_; e.detail = "not blocked"; }
  return e;
}

Explanation Workload::explainRestartAllowed() const {
  Explanation e; e.from = state_;
  if (!restartAllowed(restartCount_, policy_.restart.maxRestarts)) { e.outcome = Outcome::REJECT_RESTART_LIMIT; e.detail = "count=" + std::to_string(restartCount_) + " max=" + std::to_string(policy_.restart.maxRestarts); }
  else { e.outcome = Outcome::ALLOW; e.detail = "count=" + std::to_string(restartCount_) + " max=" + std::to_string(policy_.restart.maxRestarts); }
  return e;
}

Explanation Workload::explainMigrationAllowed() const {
  Explanation e; e.from = state_;
  if (policy_.migration.eligibility == MigrationEligibility::NONE) { e.outcome = Outcome::REJECT_MIGRATION_INELIGIBLE; e.detail = "eligibility=NONE"; }
  else { e.outcome = Outcome::ALLOW; e.detail = "eligibility=" + std::string(migrationEligibilityName(policy_.migration.eligibility)); }
  return e;
}

}  // namespace wf
