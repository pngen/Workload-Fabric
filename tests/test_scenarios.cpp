#include "test_framework.hpp"
#include "workload_fabric/coordinator.hpp"
#include "workload_fabric/persistence.hpp"
#include "workload_fabric/state.hpp"
#include "workload_fabric/reference.hpp"
#include "workload_fabric/execution.hpp"

using namespace wf;

static Policy pol() {
  Policy p;
  p.restart.kind = RestartKind::ALWAYS_WITH_LIMIT; p.restart.maxRestarts = 2;
  p.completion.kind = CompletionKind::SINGLE_RESULT;
  p.migration.eligibility = MigrationEligibility::CHECKPOINT_AND_RESUME;
  p.suspension.allowed = true;
  p.cancelRace = CancelRaceSemantics::COMPLETION_FIRST;
  p.generation = PolicyGeneration::fromU64(1);
  return p;
}

static void makeReady(WorkloadEngine& eng, WorkloadId id, std::string name) {
  (void)eng.create(id, name, "scenario", pol());
  (void)eng.setPolicy(id, pol());
  (void)eng.addResourceRequirement(id, {ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""});
  auto gen = eng.snapshot(id).contract().generation();
  (void)eng.applyGrant(id, {ResourceContractId::fromU64(10), gen, GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "gpu0"}}, false});
  (void)eng.validate(id);
  (void)eng.settle(id);
}

void testDependencyGating() {
  CASE("dependency-gating");
  WorkloadEngine eng;
  (void)eng.create(WorkloadId::fromU64(20), "dep", "gate", pol());
  (void)eng.setPolicy(WorkloadId::fromU64(20), pol());
  (void)eng.addResourceRequirement(WorkloadId::fromU64(20), {ResourceKind::HOST_MEMORY, NeedLevel::REQUIRED, 1024, 0, ""});
  auto gen = eng.snapshot(WorkloadId::fromU64(20)).contract().generation();
  (void)eng.applyGrant(WorkloadId::fromU64(20), {ResourceContractId::fromU64(11), gen, GrantStatus::GRANTED, {{ResourceKind::HOST_MEMORY, 1024, "ram"}}, false});
  CHECK(eng.addDependency(WorkloadId::fromU64(20), {WorkloadId::fromU64(21), DependencyRequirement::REQUIRED, DependencyReady::COMPLETED, ""}).outcome == Outcome::ALLOW);
  CHECK(eng.addDependency(WorkloadId::fromU64(20), {WorkloadId::fromU64(22), DependencyRequirement::OPTIONAL, DependencyReady::READY, ""}).outcome == Outcome::ALLOW);
  CHECK(eng.validate(WorkloadId::fromU64(20)).outcome == Outcome::ALLOW);
  CHECK(eng.settle(WorkloadId::fromU64(20)).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(20)).state() == WorkloadState::WAITING_FOR_DEPENDENCIES);
  CHECK(eng.setDependencyStatus(WorkloadId::fromU64(20), 1, DependencyStatus::FAILED, DependencyGeneration::fromU64(1)).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(20)).state() == WorkloadState::WAITING_FOR_DEPENDENCIES);
  CHECK(eng.setDependencyStatus(WorkloadId::fromU64(20), 0, DependencyStatus::SATISFIED, DependencyGeneration::fromU64(1)).outcome == Outcome::ALLOW);
  CHECK(eng.settle(WorkloadId::fromU64(20)).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(20)).state() == WorkloadState::READY);
  CHECK(eng.setDependencyStatus(WorkloadId::fromU64(20), 0, DependencyStatus::PENDING, DependencyGeneration::fromU64(0)).outcome == Outcome::REJECT_STALE_DEPENDENCY);
}

void testDependencyBlockingOrder() {
  CASE("dependency-blocking-order");
  WorkloadEngine eng;
  (void)eng.create(WorkloadId::fromU64(23), "depx", "gate", pol());
  (void)eng.setPolicy(WorkloadId::fromU64(23), pol());
  (void)eng.addDependency(WorkloadId::fromU64(23), {WorkloadId::fromU64(40), DependencyRequirement::REQUIRED, DependencyReady::COMPLETED, ""});
  (void)eng.validate(WorkloadId::fromU64(23));
  CHECK(eng.settle(WorkloadId::fromU64(23)).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(23)).state() == WorkloadState::WAITING_FOR_DEPENDENCIES);
}

void testResourceGenerationFencing() {
  CASE("resource-generation-fence");
  Workload w = Workload::create(WorkloadId::fromU64(30), "r", "res");
  w.setPolicyForTest(pol());
  w.contract().setIdForTest(ResourceContractId(10));
  CHECK(w.contract().add({ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""}) == Outcome::ALLOW);
  ResourceContractGeneration cur = w.contract().generation();
  CHECK(w.contract().applyGrant({ResourceContractId(10), ResourceContractGeneration::fromU64(1).previous(), GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "g"}}, false}) == Outcome::ALLOW);
  CHECK(w.contract().grant().status == GrantStatus::STALE);
  CHECK(!w.contract().satisfied());
  CHECK(w.contract().applyGrant({ResourceContractId(10), cur, GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "g"}}, false}) == Outcome::ALLOW);
  CHECK(w.contract().satisfied());
  CHECK(w.contract().releaseGrant(cur.previous()) == Outcome::REJECT_STALE_RESOURCE_CONTRACT);
  CHECK(w.contract().releaseGrant(cur) == Outcome::ALLOW);
  CHECK(w.contract().releaseGrant(cur) == Outcome::REJECT_RESOURCE_DOUBLE_RELEASE);
  CHECK(w.contract().add({ResourceKind::ACCELERATOR_COUNT, NeedLevel::PREFERRED, 2, 0, ""}) == Outcome::REJECT_IMMUTABLE_CONTRACT);
}

void testPriority() {
  CASE("priority");
  Priority p;
  p.setBase(PriorityClass(1), PriorityTier::NORMAL, 50);
  CHECK(p.effectiveValue() == 50);
  p.promote(30);
  CHECK(p.effectiveValue() == 80);
  p.demote(10);
  CHECK(p.effectiveValue() == 70);
  p.markStarved();
  CHECK(p.effectiveValue() == 1070);
  CHECK(p.mayBePreempted());
  p.setProtected(true);
  CHECK(!p.mayBePreempted());
}

void testCancellationCompletionRace() {
  CASE("cancellation-race");
  {
    WorkloadEngine eng;
    makeReady(eng, WorkloadId::fromU64(50), "race1");
    auto se = eng.startEpisode(WorkloadId::fromU64(50), OwnerId(1), OwnerGeneration::fromU64(1));
    auto ep = se.episodeId.value();
    auto gen = eng.snapshot(WorkloadId::fromU64(50)).generation();
    CHECK(eng.admitDispatch(WorkloadId::fromU64(50), ep, WorkerId(1), WorkerBootId(1)).outcome == Outcome::ALLOW);
    CHECK(eng.cancel(WorkloadId::fromU64(50), gen).outcome == Outcome::ALLOW);
    CHECK(eng.episodeCompleted(WorkloadId::fromU64(50), ep).outcome == Outcome::ALLOW);
    CHECK(eng.commitCompletion(WorkloadId::fromU64(50), gen).outcome == Outcome::ALLOW);
    CHECK(eng.snapshot(WorkloadId::fromU64(50)).state() == WorkloadState::COMPLETED);
    CHECK(eng.cancel(WorkloadId::fromU64(50), gen).outcome == Outcome::REJECT_TERMINAL);
  }
  {
    WorkloadEngine eng;
    Policy p = pol(); p.cancelRace = CancelRaceSemantics::CANCEL_FIRST;
    (void)eng.create(WorkloadId::fromU64(51), "race2", "x", pol());
    (void)eng.setPolicy(WorkloadId::fromU64(51), p);
    (void)eng.addResourceRequirement(WorkloadId::fromU64(51), {ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""});
    auto gen = eng.snapshot(WorkloadId::fromU64(51)).contract().generation();
    (void)eng.applyGrant(WorkloadId::fromU64(51), {ResourceContractId(10), gen, GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "g"}}, false});
    (void)eng.validate(WorkloadId::fromU64(51));
    (void)eng.settle(WorkloadId::fromU64(51));
    auto se = eng.startEpisode(WorkloadId::fromU64(51), OwnerId(1), OwnerGeneration::fromU64(1));
    auto ep = se.episodeId.value();
    auto gen2 = eng.snapshot(WorkloadId::fromU64(51)).generation();
    CHECK(eng.admitDispatch(WorkloadId::fromU64(51), ep, WorkerId(1), WorkerBootId(1)).outcome == Outcome::ALLOW);
    CHECK(eng.cancel(WorkloadId::fromU64(51), gen2).outcome == Outcome::ALLOW);
    (void)eng.episodeCompleted(WorkloadId::fromU64(51), ep);
    CHECK(eng.commitCompletion(WorkloadId::fromU64(51), gen2).outcome == Outcome::REJECT_CANCELLED);
    CHECK(eng.completeCancellation(WorkloadId::fromU64(51), gen2).outcome == Outcome::ALLOW);
    CHECK(eng.snapshot(WorkloadId::fromU64(51)).state() == WorkloadState::CANCELLED);
  }
}

void testMigrationLifecycle() {
  CASE("migration");
  WorkloadEngine eng;
  makeReady(eng, WorkloadId::fromU64(60), "mig");
  auto se = eng.startEpisode(WorkloadId::fromU64(60), OwnerId(1), OwnerGeneration::fromU64(1));
  auto ep1 = se.episodeId.value();
  auto gen = eng.snapshot(WorkloadId::fromU64(60)).generation();
  CHECK(eng.admitDispatch(WorkloadId::fromU64(60), ep1, WorkerId(1), WorkerBootId(1)).outcome == Outcome::ALLOW);
  CHECK(eng.progress(WorkloadId::fromU64(60), ep1, ExecutionEpisodeGeneration::fromU64(1), 7, true).outcome == Outcome::ALLOW);
  CHECK(eng.requestMigration(WorkloadId::fromU64(60), gen).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(60)).state() == WorkloadState::MIGRATION_PENDING);
  CHECK(eng.commitMigration(WorkloadId::fromU64(60), gen, WorkerId(2), WorkerBootId(2)).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(60)).state() == WorkloadState::RUNNING);
  auto ep2 = eng.snapshot(WorkloadId::fromU64(60)).currentEpisodeId();
  CHECK(ep2 != ep1);
  CHECK(eng.progress(WorkloadId::fromU64(60), ep1, ExecutionEpisodeGeneration::fromU64(1), 5, true).outcome == Outcome::REJECT_EPISODE_INELIGIBLE);
  CHECK(eng.progress(WorkloadId::fromU64(60), ep2, ExecutionEpisodeGeneration::fromU64(1), 5, true).outcome == Outcome::ALLOW);
}

void testSuspensionLifecycle() {
  CASE("suspension");
  WorkloadEngine eng;
  makeReady(eng, WorkloadId::fromU64(70), "susp");
  auto se = eng.startEpisode(WorkloadId::fromU64(70), OwnerId(1), OwnerGeneration::fromU64(1));
  auto ep1 = se.episodeId.value();
  auto gen = eng.snapshot(WorkloadId::fromU64(70)).generation();
  CHECK(eng.admitDispatch(WorkloadId::fromU64(70), ep1, WorkerId(1), WorkerBootId(1)).outcome == Outcome::ALLOW);
  CHECK(eng.progress(WorkloadId::fromU64(70), ep1, ExecutionEpisodeGeneration::fromU64(1), 3, true).outcome == Outcome::ALLOW);
  CHECK(eng.requestSuspend(WorkloadId::fromU64(70), gen).outcome == Outcome::ALLOW);
  CHECK(eng.completeSuspend(WorkloadId::fromU64(70), gen).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(70)).state() == WorkloadState::SUSPENDED);
  CHECK(eng.snapshot(WorkloadId::fromU64(70)).progress().completed == 3);
  CHECK(eng.requestResume(WorkloadId::fromU64(70), gen).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(70)).state() == WorkloadState::READY);
  auto se2 = eng.startEpisode(WorkloadId::fromU64(70), OwnerId(2), OwnerGeneration::fromU64(1));
  CHECK(se2.outcome == Outcome::ALLOW);
  auto ep2 = se2.episodeId.value();
  CHECK(ep2 != ep1);
  CHECK(eng.snapshot(WorkloadId::fromU64(70)).state() == WorkloadState::STARTING);
}

void testRestartNeverPolicy() {
  CASE("restart-never");
  WorkloadEngine eng;
  (void)eng.create(WorkloadId::fromU64(80), "never", "x", pol());
  Policy p = pol(); p.restart.kind = RestartKind::NEVER; p.restart.maxRestarts = 0;
  (void)eng.setPolicy(WorkloadId::fromU64(80), p);
  (void)eng.addResourceRequirement(WorkloadId::fromU64(80), {ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""});
  auto gen = eng.snapshot(WorkloadId::fromU64(80)).contract().generation();
  (void)eng.applyGrant(WorkloadId::fromU64(80), {ResourceContractId(10), gen, GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "g"}}, false});
  (void)eng.validate(WorkloadId::fromU64(80));
  (void)eng.settle(WorkloadId::fromU64(80));
  auto se = eng.startEpisode(WorkloadId::fromU64(80), OwnerId(1), OwnerGeneration::fromU64(1));
  auto ep = se.episodeId.value();
  auto gen2 = eng.snapshot(WorkloadId::fromU64(80)).generation();
  (void)eng.admitDispatch(WorkloadId::fromU64(80), ep, WorkerId(1), WorkerBootId(1));
  CHECK(eng.onWorkerLost(WorkloadId::fromU64(80), WorkerId(1), WorkerBootId(1), gen2).outcome == Outcome::ALLOW);
  CHECK(eng.planRestart(WorkloadId::fromU64(80), "WORKER_LOSS", gen2).outcome == Outcome::REJECT_RESTART_LIMIT);
}

void testReferenceExecutionFabric() {
  CASE("reference-execution");
  ReferenceExecutionFabric ef;
  ExecutionEpisodeId ep(1);
  CHECK(ef.requestExecution(ep, "job") == Outcome::ALLOW);
  auto h = ef.currentHandle(ep);
  CHECK(h.has_value());
  ExecutionState st;
  CHECK(ef.observeExecution(*h, st) == Outcome::ALLOW);
  bool author = true;
  CHECK(ef.observeCompletion(*h, author) == Outcome::ALLOW);
  CHECK(author == false);
  ef.reportCompletionForTest(ep, 1);
  CHECK(ef.observeCompletion(*h, author) == Outcome::ALLOW);
  CHECK(author == true);
}

int main() {
  testDependencyGating();
  testDependencyBlockingOrder();
  testResourceGenerationFencing();
  testPriority();
  testCancellationCompletionRace();
  testMigrationLifecycle();
  testSuspensionLifecycle();
  testRestartNeverPolicy();
  testReferenceExecutionFabric();
  TEST_MAIN_END();
}
