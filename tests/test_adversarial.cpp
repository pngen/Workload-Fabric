#include "test_framework.hpp"
#include "workload_fabric/coordinator.hpp"
#include "workload_fabric/persistence.hpp"
#include "workload_fabric/state.hpp"
#include <cstdint>

using namespace wf;

// Adversarial searches for specific defect classes.

static Policy pol() {
  Policy p;
  p.restart.kind = RestartKind::ALWAYS_WITH_LIMIT; p.restart.maxRestarts = 2;
  p.completion.kind = CompletionKind::SINGLE_RESULT;
  p.migration.eligibility = MigrationEligibility::CHECKPOINT_AND_RESUME;
  p.suspension.allowed = true;
  p.generation = PolicyGeneration::fromU64(1);
  return p;
}

static void makeReady(WorkloadEngine& eng, WorkloadId id) {
  (void)eng.create(id, "adv", "x", pol());
  (void)eng.addResourceRequirement(id, {ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""});
  auto gen = eng.snapshot(id).contract().generation();
  (void)eng.applyGrant(id, {ResourceContractId(10), gen, GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "g"}}, false});
  (void)eng.validate(id);
  (void)eng.settle(id);
}

void testProgressDoubleCount() {
  CASE("adversarial-progress-double-count");
  Workload w = Workload::create(WorkloadId(1), "n", "i");
  w.setPolicyForTest(pol());
  w.contract().setIdForTest(ResourceContractId(10));
  w.contract().add({ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""});
  w.contract().applyGrant({ResourceContractId(10), w.contract().generation(), GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "g"}}, false});
  w.transition(WorkloadState::VALIDATING, w.generation(), "v");
  w.settle(w.generation());
  CHECK(w.state() == WorkloadState::READY);
  w.startEpisode(ExecutionEpisodeId(1), OwnerId(1), OwnerGeneration::fromU64(1), true);
  w.admitDispatch(ExecutionEpisodeId(1), WorkerId(1), WorkerBootId(1));
  w.progress().totalKind = TotalKind::KNOWN;
  w.progress().total = 10;
  CHECK(w.reportProgress(ExecutionEpisodeId(1), ExecutionEpisodeGeneration::fromU64(1), 8, true) == Outcome::ALLOW);
  CHECK(w.progress().completed == 8);
  CHECK(w.reportProgress(ExecutionEpisodeId(1), ExecutionEpisodeGeneration::fromU64(1), 8, true) == Outcome::REJECT_PROGRESS_DOUBLE_COUNT);
  CHECK(w.progress().completed == 8);
  CHECK(w.reportProgress(ExecutionEpisodeId(1), ExecutionEpisodeGeneration::fromU64(1), 2, true) == Outcome::ALLOW);
  CHECK(w.progress().completed == 10);
}

void testDuplicateWorkloadPublication() {
  CASE("adversarial-duplicate-publication");
  WorkloadEngine eng;
  (void)eng.create(WorkloadId(1), "a", "x", pol());
  CHECK(eng.create(WorkloadId(1), "b", "x", pol()).outcome == Outcome::REJECT_DUPLICATE_WORKLOAD);
  CHECK(eng.list().size() == 1);
}

void testMigrationSplitBrain() {
  CASE("adversarial-migration-split-brain");
  WorkloadEngine eng;
  makeReady(eng, WorkloadId(1));
  auto se = eng.startEpisode(WorkloadId(1), OwnerId(1), OwnerGeneration::fromU64(1));
  auto gen = eng.snapshot(WorkloadId(1)).generation();
  (void)eng.admitDispatch(WorkloadId(1), se.episodeId.value(), WorkerId(1), WorkerBootId(1));
  CHECK(eng.requestMigration(WorkloadId(1), gen).outcome == Outcome::ALLOW);
  CHECK(eng.commitMigration(WorkloadId(1), gen, WorkerId(2), WorkerBootId(2)).outcome == Outcome::ALLOW);
  auto gen2 = eng.snapshot(WorkloadId(1)).generation();
  CHECK(eng.requestMigration(WorkloadId(1), gen2).outcome == Outcome::ALLOW);
  CHECK(eng.commitMigration(WorkloadId(1), gen2, WorkerId(3), WorkerBootId(3)).outcome == Outcome::ALLOW);
  Workload fin = eng.snapshot(WorkloadId(1));
  int auth = 0;
  for (const auto& e : fin.episodes()) if (e.authoritative) ++auth;
  CHECK(auth == 1);
  auto ep1 = fin.episodes().front().id;
  CHECK(eng.progress(WorkloadId(1), ep1, ExecutionEpisodeGeneration::fromU64(1), 1, true).outcome == Outcome::REJECT_EPISODE_INELIGIBLE);
}

void testRecoveryNoResurrection() {
  CASE("adversarial-recovery-no-resurrection");
  WorkloadEngine eng;
  makeReady(eng, WorkloadId(1));
  auto se = eng.startEpisode(WorkloadId(1), OwnerId(1), OwnerGeneration::fromU64(1));
  auto gen = eng.snapshot(WorkloadId(1)).generation();
  (void)eng.admitDispatch(WorkloadId(1), se.episodeId.value(), WorkerId(7), WorkerBootId(77));
  (void)eng.progress(WorkloadId(1), se.episodeId.value(), ExecutionEpisodeGeneration::fromU64(1), 4, true);
  auto bytes = encodeWorkload(eng.snapshot(WorkloadId(1)));
  Outcome st = Outcome::UNKNOWN;
  Workload r = decodeWorkload(bytes, st);
  CHECK(isAllow(st));
  CHECK(r.state() == WorkloadState::RUNNING);
  CHECK(r.currentEpisodeId().valid());
  CHECK(r.onWorkerLost(WorkerId(7), WorkerBootId(999), gen) == Outcome::REJECT_STALE_BOOT);
  CHECK(r.reportProgress(r.currentEpisodeId(), ExecutionEpisodeGeneration::fromU64(99), 1, true) == Outcome::REJECT_STALE_EPISODE);
}

void testTerminalNeverReopens() {
  CASE("adversarial-terminal-never-reopens");
  WorkloadEngine eng;
  makeReady(eng, WorkloadId(1));
  auto se = eng.startEpisode(WorkloadId(1), OwnerId(1), OwnerGeneration::fromU64(1));
  auto gen = eng.snapshot(WorkloadId(1)).generation();
  (void)eng.admitDispatch(WorkloadId(1), se.episodeId.value(), WorkerId(1), WorkerBootId(1));
  (void)eng.episodeCompleted(WorkloadId(1), se.episodeId.value());
  CHECK(eng.commitCompletion(WorkloadId(1), gen).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId(1)).state() == WorkloadState::COMPLETED);
  CHECK(eng.cancel(WorkloadId(1), gen).outcome == Outcome::REJECT_TERMINAL);
  CHECK(eng.planRestart(WorkloadId(1), "x", gen).outcome == Outcome::REJECT_TERMINAL);
  CHECK(eng.startEpisode(WorkloadId(1), OwnerId(1), OwnerGeneration::fromU64(1)).outcome == Outcome::REJECT_TERMINAL);
  CHECK(eng.progress(WorkloadId(1), se.episodeId.value(), ExecutionEpisodeGeneration::fromU64(1), 1, true).outcome == Outcome::REJECT_TERMINAL);
}

void testCrossWorkloadIdentity() {
  CASE("adversarial-cross-workload-identity");
  WorkloadEngine eng;
  makeReady(eng, WorkloadId(1));
  auto se = eng.startEpisode(WorkloadId(1), OwnerId(1), OwnerGeneration::fromU64(1));
  auto gen = eng.snapshot(WorkloadId(1)).generation();
  (void)eng.admitDispatch(WorkloadId(1), se.episodeId.value(), WorkerId(1), WorkerBootId(1));
  CHECK(eng.cancel(WorkloadId(1), gen.previous()).outcome == Outcome::REJECT_STALE_WORKLOAD_GENERATION);
  CHECK(eng.snapshot(WorkloadId(1)).generation().valid());
}

int main() {
  testProgressDoubleCount();
  testDuplicateWorkloadPublication();
  testMigrationSplitBrain();
  testRecoveryNoResurrection();
  testTerminalNeverReopens();
  testCrossWorkloadIdentity();
  TEST_MAIN_END();
}
