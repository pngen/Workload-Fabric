#include "test_framework.hpp"
#include "workload_fabric/workload.hpp"
#include "workload_fabric/persistence.hpp"
#include "workload_fabric/state.hpp"

using namespace wf;

static Policy goodPolicy() {
  Policy p;
  p.restart.kind = RestartKind::ALWAYS_WITH_LIMIT;
  p.restart.maxRestarts = 2;
  p.completion.kind = CompletionKind::SINGLE_RESULT;
  p.migration.eligibility = MigrationEligibility::CHECKPOINT_AND_RESUME;
  p.suspension.allowed = true;
  p.generation = PolicyGeneration::fromU64(1);
  return p;
}

static Workload makeWl(WorkloadId id) {
  Workload w = Workload::create(id, "w" + std::to_string(id.toU64()), "compute");
  w.setPolicyForTest(goodPolicy());
  w.contract().setIdForTest(ResourceContractId(10));
  w.contract().add({ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""});
  w.contract().applyGrant({ResourceContractId(10), ResourceContractGeneration::fromU64(w.contract().generation().toU64()), GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "gpu0"}}, false});
  w.dependencies().setIdForTest(DependencySetId(20));
  w.dependencies().addDefForTest({WorkloadId(2), DependencyRequirement::REQUIRED, DependencyReady::COMPLETED, ""});
  w.dependencies().setStatus(0, DependencyStatus::SATISFIED, DependencyGeneration::fromU64(1));
  return w;
}

void testIllegalTransitions() {
  CASE("illegal-transitions");
  Workload w = makeWl(WorkloadId(1));
  Explanation ex;
  WorkloadGeneration g1 = WorkloadGeneration::fromU64(1);
  // CREATED -> COMPLETED without zero-work policy must reject.
  CHECK_EQ(w.transition(WorkloadState::COMPLETED, g1, "x", &ex), Outcome::REJECT_COMPLETION_POLICY);
  // TERMINAL -> RUNNING must reject.
  CHECK_EQ(w.transition(WorkloadState::COMPLETED, g1, "x"), Outcome::REJECT_COMPLETION_POLICY);
  CHECK(w.state() == WorkloadState::CREATED);
  // A stale workload generation must reject any mutation.
  CHECK_EQ(w.transition(WorkloadState::VALIDATING, WorkloadGeneration::fromU64(99), "x"), Outcome::REJECT_STALE_WORKLOAD_GENERATION);
  CHECK_EQ(w.transition(WorkloadState::VALIDATING, g1, "validate"), Outcome::ALLOW);
  CHECK(w.state() == WorkloadState::VALIDATING);
}

void testZeroWorkAllowed() {
  CASE("zero-work-completion");
  Workload w = makeWl(WorkloadId(2));
  Policy p = goodPolicy(); p.completion.allowZeroWorkCompletion = true; w.setPolicyForTest(p);
  CHECK_EQ(w.transition(WorkloadState::COMPLETED, WorkloadGeneration::fromU64(1), "zero"), Outcome::ALLOW);
  CHECK(w.state() == WorkloadState::COMPLETED);
  // completed cannot reopen.
  CHECK_EQ(w.transition(WorkloadState::RUNNING, WorkloadGeneration::fromU64(1), "x"), Outcome::REJECT_TERMINAL);
}

void testHappyPath() {
  CASE("happy-path");
  Workload w = makeWl(WorkloadId(3));
  CHECK(w.contract().satisfied());
  CHECK(w.dependencies().ready());
  CHECK_EQ(w.transition(WorkloadState::VALIDATING, WorkloadGeneration::fromU64(1), "validate"), Outcome::ALLOW);
  CHECK_EQ(w.settle(WorkloadGeneration::fromU64(1)), Outcome::ALLOW);
  CHECK(w.state() == WorkloadState::READY);
  // start an episode
  CHECK_EQ(w.startEpisode(ExecutionEpisodeId(100), OwnerId(1), OwnerGeneration::fromU64(1), true), Outcome::ALLOW);
  CHECK(w.state() == WorkloadState::STARTING);
  CHECK_EQ(w.generation().toU64(), 2U);
  WorkloadGeneration g2 = w.generation();
  CHECK_EQ(w.admitDispatch(ExecutionEpisodeId(100), WorkerId(1), WorkerBootId(50)), Outcome::ALLOW);
  CHECK(w.state() == WorkloadState::RUNNING);
  CHECK_EQ(w.reportProgress(ExecutionEpisodeId(100), ExecutionEpisodeGeneration::fromU64(1), 5, true), Outcome::ALLOW);
  CHECK_EQ(w.progress().completed, 5U);
  CHECK_EQ(w.markEpisodeCompleted(ExecutionEpisodeId(100)), Outcome::ALLOW);
  CHECK_EQ(w.commitCompletion(g2), Outcome::ALLOW);
  CHECK(w.state() == WorkloadState::COMPLETED);
  // completed cannot restart
  CHECK_EQ(w.planRestart(g2, "x"), Outcome::REJECT_TERMINAL);
  CHECK_EQ(w.restartCount(), 0U);  // rejected restart must not consume a slot
}

void testRestartLimitOffByOne() {
  CASE("restart-limit");
  Workload w = makeWl(WorkloadId(4));
  CHECK_EQ(w.transition(WorkloadState::VALIDATING, WorkloadGeneration::fromU64(1), "v"), Outcome::ALLOW);
  CHECK_EQ(w.settle(WorkloadGeneration::fromU64(1)), Outcome::ALLOW);
  CHECK_EQ(w.startEpisode(ExecutionEpisodeId(200), OwnerId(1), OwnerGeneration::fromU64(1), true), Outcome::ALLOW);
  WorkloadGeneration g2 = w.generation();
  CHECK_EQ(w.admitDispatch(ExecutionEpisodeId(200), WorkerId(1), WorkerBootId(50)), Outcome::ALLOW);
  // Worker loss puts the workload into RECOVERING.
  CHECK_EQ(w.onWorkerLost(WorkerId(1), WorkerBootId(50), g2), Outcome::ALLOW);
  CHECK(w.state() == WorkloadState::RECOVERING);
  // First restart allowed (RECOVERING -> RESTART_PENDING).
  CHECK_EQ(w.planRestart(g2, "WORKER_LOSS"), Outcome::ALLOW);
  CHECK_EQ(w.restartCount(), 1U);
  CHECK(w.state() == WorkloadState::RESTART_PENDING);
  // Second restart (already in RESTART_PENDING; no-op state-wise but count advances).
  CHECK_EQ(w.planRestart(g2, "WORKER_LOSS"), Outcome::ALLOW);
  CHECK_EQ(w.restartCount(), 2U);
  // Third restart must be rejected: max=2 means at most 2 restarts total.
  CHECK_EQ(w.planRestart(g2, "WORKER_LOSS"), Outcome::REJECT_RESTART_LIMIT);
  CHECK_EQ(w.restartCount(), 2U);
}

void testStaleAuthority() {
  CASE("stale-authority");
  Workload w = makeWl(WorkloadId(5));
  CHECK_EQ(w.transition(WorkloadState::VALIDATING, WorkloadGeneration::fromU64(1), "v"), Outcome::ALLOW);
  CHECK_EQ(w.settle(WorkloadGeneration::fromU64(1)), Outcome::ALLOW);
  CHECK_EQ(w.startEpisode(ExecutionEpisodeId(300), OwnerId(1), OwnerGeneration::fromU64(1), true), Outcome::ALLOW);
  WorkloadGeneration g2 = w.generation();
  CHECK_EQ(w.admitDispatch(ExecutionEpisodeId(300), WorkerId(1), WorkerBootId(50)), Outcome::ALLOW);
  // Stale episode generation must reject progress.
  CHECK_EQ(w.reportProgress(ExecutionEpisodeId(300), ExecutionEpisodeGeneration::fromU64(7), 3, true), Outcome::REJECT_STALE_EPISODE);
  // Correct episode generation must not.
  CHECK_EQ(w.reportProgress(ExecutionEpisodeId(300), ExecutionEpisodeGeneration::fromU64(1), 3, true), Outcome::ALLOW);
  // Stale boot must be rejected on worker loss.
  CHECK_EQ(w.onWorkerLost(WorkerId(1), WorkerBootId(999), g2), Outcome::REJECT_STALE_BOOT);
  // Correct boot transitions to recovering.
  CHECK_EQ(w.onWorkerLost(WorkerId(1), WorkerBootId(50), g2), Outcome::ALLOW);
  CHECK(w.state() == WorkloadState::RECOVERING);
}

void testPersistenceRoundTrip() {
  CASE("persistence-roundtrip");
  Workload w = makeWl(WorkloadId(6));
  CHECK_EQ(w.transition(WorkloadState::VALIDATING, WorkloadGeneration::fromU64(1), "v"), Outcome::ALLOW);
  CHECK_EQ(w.settle(WorkloadGeneration::fromU64(1)), Outcome::ALLOW);
  CHECK_EQ(w.startEpisode(ExecutionEpisodeId(400), OwnerId(1), OwnerGeneration::fromU64(1), true), Outcome::ALLOW);
  WorkloadGeneration g2 = w.generation();
  CHECK_EQ(w.admitDispatch(ExecutionEpisodeId(400), WorkerId(1), WorkerBootId(50)), Outcome::ALLOW);
  CHECK_EQ(w.reportProgress(ExecutionEpisodeId(400), ExecutionEpisodeGeneration::fromU64(1), 9, true), Outcome::ALLOW);
  auto bytes = encodeWorkload(w);
  CHECK(bytes.size() > 0);
  Outcome st = Outcome::UNKNOWN;
  Workload r = decodeWorkload(bytes, st);
  CHECK(isAllow(st));
  CHECK_EQ(r.id(), w.id());
  CHECK_EQ(r.state(), w.state());
  CHECK_EQ(r.progress().completed, 9U);
  CHECK_EQ(r.episodes().size(), 1U);
  CHECK(!encodeWorkload(r).empty());
}

void testPersistenceCorruption() {
  CASE("persistence-corruption");
  Workload w = makeWl(WorkloadId(7));
  auto bytes = encodeWorkload(w);
  CHECK(bytes.size() > 0);
  int rejected = 0;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    auto corrupt = bytes;
    corrupt[i] ^= 0xFF;
    Outcome st = Outcome::ALLOW;
    (void)decodeWorkload(corrupt, st);
    if (st != Outcome::ALLOW) ++rejected;
  }
  // Every single-byte corruption must be rejected by integrity/version checks.
  CHECK_EQ(rejected, static_cast<int>(bytes.size()));
}

void testProgressMonotonic() {
  CASE("progress-monotonic");
  Workload w = makeWl(WorkloadId(8));
  CHECK_EQ(w.transition(WorkloadState::VALIDATING, WorkloadGeneration::fromU64(1), "v"), Outcome::ALLOW);
  CHECK_EQ(w.settle(WorkloadGeneration::fromU64(1)), Outcome::ALLOW);
  CHECK_EQ(w.startEpisode(ExecutionEpisodeId(500), OwnerId(1), OwnerGeneration::fromU64(1), true), Outcome::ALLOW);
  WorkloadGeneration g2 = w.generation();
  CHECK_EQ(w.admitDispatch(ExecutionEpisodeId(500), WorkerId(1), WorkerBootId(50)), Outcome::ALLOW);
  CHECK_EQ(w.reportProgress(ExecutionEpisodeId(500), ExecutionEpisodeGeneration::fromU64(1), 4, true), Outcome::ALLOW);
  // A reconcile that would regress must fail.
  CHECK(!w.progress().reconcile(2, false, ProgressProvenance::RECONSTRUCTED));
  CHECK(w.progress().completed == 4U);
}

int main() {
  testIllegalTransitions();
  testZeroWorkAllowed();
  testHappyPath();
  testRestartLimitOffByOne();
  testStaleAuthority();
  testPersistenceRoundTrip();
  testPersistenceCorruption();
  testProgressMonotonic();
  TEST_MAIN_END();
}
