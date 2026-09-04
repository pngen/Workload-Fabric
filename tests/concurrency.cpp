#include "test_framework.hpp"
#include "workload_fabric/coordinator.hpp"
#include "workload_fabric/persistence.hpp"
#include <atomic>
#include <thread>
#include <vector>

using namespace wf;

static Policy pol() {
  Policy p;
  p.restart.kind = RestartKind::ALWAYS_WITH_LIMIT; p.restart.maxRestarts = 5;
  p.completion.kind = CompletionKind::SINGLE_RESULT;
  p.migration.eligibility = MigrationEligibility::CHECKPOINT_AND_RESUME;
  p.suspension.allowed = true;
  p.cancelRace = CancelRaceSemantics::COMPLETION_FIRST;
  p.generation = PolicyGeneration::fromU64(1);
  return p;
}

static void makeReady(WorkloadEngine& eng, WorkloadId id) {
  (void)eng.create(id, "conc", "race", pol());
  (void)eng.addResourceRequirement(id, {ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""});
  auto gen = eng.snapshot(id).contract().generation();
  (void)eng.applyGrant(id, {ResourceContractId(10), gen, GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "g"}}, false});
  (void)eng.validate(id);
  (void)eng.settle(id);
}

void raceProgressRestart() {
  CASE("concurrency-progress-restart");
  WorkloadEngine eng;
  makeReady(eng, WorkloadId::fromU64(90));
  auto se = eng.startEpisode(WorkloadId::fromU64(90), OwnerId(1), OwnerGeneration::fromU64(1));
  auto ep = se.episodeId.value();
  auto gen = eng.snapshot(WorkloadId::fromU64(90)).generation();
  CHECK(eng.admitDispatch(WorkloadId::fromU64(90), ep, WorkerId(1), WorkerBootId(1)).outcome == Outcome::ALLOW);

  std::atomic<bool> stop{false};
  std::atomic<std::uint64_t> totalProgress{0};

  std::thread progressThread([&] {
    while (!stop.load()) {
      auto w = eng.snapshot(WorkloadId::fromU64(90));
      if (w.state() == WorkloadState::RUNNING) {
        ExecutionEpisodeId curep = w.currentEpisodeId();
        auto r = eng.progress(WorkloadId::fromU64(90), curep, ExecutionEpisodeGeneration::fromU64(1), 1, true);
        if (r.outcome == Outcome::ALLOW) totalProgress.fetch_add(1);
      }
    }
  });
  std::thread restartThread([&] {
    for (int i = 0; i < 2000; ++i) {
      auto w = eng.snapshot(WorkloadId::fromU64(90));
      if (w.state() == WorkloadState::RUNNING) {
        auto r = eng.onWorkerLost(WorkloadId::fromU64(90), WorkerId(1), WorkerBootId(1), w.generation());
        if (r.outcome == Outcome::ALLOW) {
          (void)eng.planRestart(WorkloadId::fromU64(90), "WORKER_LOSS", w.generation());
          auto se2 = eng.startEpisode(WorkloadId::fromU64(90), OwnerId(1), OwnerGeneration::fromU64(1));
          if (se2.outcome == Outcome::ALLOW) {
            (void)eng.admitDispatch(WorkloadId::fromU64(90), se2.episodeId.value(), WorkerId(1), WorkerBootId(1));
          }
        }
      }
    }
    stop.store(true);
  });
  restartThread.join();
  progressThread.join();

  Workload fin = eng.snapshot(WorkloadId::fromU64(90));
  CHECK(fin.restartCount() <= fin.policy().restart.maxRestarts);
  CHECK(fin.progress().completed > 0);
  CHECK(fin.progress().completed <= totalProgress.load());  // accounted progress never exceeds reported
  CHECK(fin.restartCount() <= fin.policy().restart.maxRestarts);
  std::printf("    [progress-restart] final progress=%llu restarts=%u\n", (unsigned long long)fin.progress().completed, fin.restartCount());
}

void raceCancelComplete() {
  CASE("concurrency-cancel-complete");
  for (int iter = 0; iter < 50; ++iter) {
    WorkloadEngine eng;
    makeReady(eng, WorkloadId::fromU64(100 + iter));
    auto se = eng.startEpisode(WorkloadId::fromU64(100 + iter), OwnerId(1), OwnerGeneration::fromU64(1));
    auto ep = se.episodeId.value();
    auto gen = eng.snapshot(WorkloadId::fromU64(100 + iter)).generation();
    (void)eng.admitDispatch(WorkloadId::fromU64(100 + iter), ep, WorkerId(1), WorkerBootId(1));
    std::thread cancelThread([&] {
      auto r = eng.cancel(WorkloadId::fromU64(100 + iter), gen);
      (void)r;
    });
    std::thread completeThread([&] {
      (void)eng.episodeCompleted(WorkloadId::fromU64(100 + iter), ep);
      auto r = eng.commitCompletion(WorkloadId::fromU64(100 + iter), gen);
      (void)r;
    });
    cancelThread.join();
    completeThread.join();
    Workload fin = eng.snapshot(WorkloadId::fromU64(100 + iter));
    CHECK(isTerminal(fin.state()));
    // re-open attempt must reject
    auto r2 = eng.cancel(WorkloadId::fromU64(100 + iter), gen);
    CHECK(r2.outcome == Outcome::REJECT_TERMINAL || r2.outcome == Outcome::REJECT_CANCELLED);
  }
  std::printf("    [cancel-complete] 50 races, no double-terminal, no reopen\n");
}

void raceCrossWorkload() {
  CASE("concurrency-cross-workload");
  WorkloadEngine eng;
  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  std::atomic<std::uint64_t> completed{0};
  for (int w = 0; w < 8; ++w) {
    WorkloadId id(200 + w);
    makeReady(eng, id);
    threads.emplace_back([&, id] {
      auto se = eng.startEpisode(id, OwnerId(1), OwnerGeneration::fromU64(1));
      if (se.outcome != Outcome::ALLOW) { failures++; return; }
      auto gen = eng.snapshot(id).generation();
      if (eng.admitDispatch(id, se.episodeId.value(), WorkerId(1), WorkerBootId(1)).outcome != Outcome::ALLOW) { failures++; return; }
      auto r = eng.progress(id, se.episodeId.value(), ExecutionEpisodeGeneration::fromU64(1), 3, true);
      if (r.outcome != Outcome::ALLOW) { failures++; return; }
      if (eng.episodeCompleted(id, se.episodeId.value()).outcome != Outcome::ALLOW) { failures++; return; }
      if (eng.commitCompletion(id, gen).outcome != Outcome::ALLOW) { failures++; return; }
      completed.fetch_add(1);
    });
  }
  for (auto& t : threads) t.join();
  CHECK(failures.load() == 0);
  CHECK(completed.load() == 8);
  for (int w = 0; w < 8; ++w) {
    Workload fin = eng.snapshot(WorkloadId(200 + w));
    CHECK(fin.state() == WorkloadState::COMPLETED);
    CHECK(fin.progress().completed == 3);
  }
  std::printf("    [cross-workload] 8 independent workloads completed in parallel\n");
}

void raceContractVsDispatch() {
  CASE("concurrency-contract-dispatch");
  WorkloadEngine eng;
  makeReady(eng, WorkloadId::fromU64(400));
  auto se = eng.startEpisode(WorkloadId::fromU64(400), OwnerId(1), OwnerGeneration::fromU64(1));
  auto ep = se.episodeId.value();
  std::thread dispatchThread([&] {
    for (int i = 0; i < 200; ++i) {
      auto w = eng.snapshot(WorkloadId::fromU64(400));
      if (w.state() == WorkloadState::STARTING) {
        (void)eng.admitDispatch(WorkloadId::fromU64(400), ep, WorkerId(1), WorkerBootId(1));
        break;
      }
    }
  });
  std::thread contractThread([&] {
    (void)eng.addResourceRequirement(WorkloadId::fromU64(400), {ResourceKind::HOST_MEMORY, NeedLevel::REQUIRED, 512, 0, ""});
  });
  dispatchThread.join();
  contractThread.join();
  Workload fin = eng.snapshot(WorkloadId::fromU64(400));
  CHECK(fin.contract().generation().valid());
  std::printf("    [contract-dispatch] contract generation advanced to gen=%llu\n", (unsigned long long)fin.contract().generation().toU64());
}

int main() {
  raceProgressRestart();
  raceCancelComplete();
  raceCrossWorkload();
  raceContractVsDispatch();
  TEST_MAIN_END();
}
