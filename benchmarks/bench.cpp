#include "workload_fabric/workload.hpp"
#include "workload_fabric/coordinator.hpp"
#include "workload_fabric/persistence.hpp"
#include "workload_fabric/protocol.hpp"
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

using namespace wf;
using Clock = std::chrono::steady_clock;

static Policy pol() {
  Policy p;
  p.restart.kind = RestartKind::ALWAYS_WITH_LIMIT; p.restart.maxRestarts = 4;
  p.completion.kind = CompletionKind::SINGLE_RESULT;
  p.migration.eligibility = MigrationEligibility::CHECKPOINT_AND_RESUME;
  p.generation = PolicyGeneration::fromU64(1);
  return p;
}

static double nsPer(std::uint64_t iters, const std::function<void()>& fn) {
  auto t0 = Clock::now();
  for (std::uint64_t i = 0; i < iters; ++i) fn();
  auto t1 = Clock::now();
  return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) / static_cast<double>(iters);
}

static void report(const char* name, double ns) {
  std::printf("  %-32s %10.1f ns/op  %10.0f ops/ms\n", name, ns, 1e6 / ns);
}

int main() {
  std::printf("Workload Fabric benchmarks (release)\n");
  const int N = 20000;

  // workload creation
  report("workload.create", nsPer(N, [] {
    auto w = Workload::create(WorkloadId(1), "n", "i");
    (void)w;
  }));

  // lifecycle transition validation
  report("lifecycle.transition", nsPer(N, [] {
    Workload w = Workload::create(WorkloadId(1), "n", "i");
    (void)w.transition(WorkloadState::VALIDATING, w.generation(), "v");
  }));

  // workload lookup (engine)
  {
    WorkloadEngine eng;
    (void)eng.create(WorkloadId::fromU64(1), "n", "i", pol());
    report("engine.lookup", nsPer(N, [&] { (void)eng.snapshot(WorkloadId::fromU64(1)); }));
  }

  // readiness evaluation / dependency gating
  {
    Workload w = Workload::create(WorkloadId(1), "n", "i");
    w.setPolicyForTest(pol());
    w.contract().setIdForTest(ResourceContractId(10));
    w.contract().add({ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""});
    w.contract().applyGrant({ResourceContractId(10), w.contract().generation(), GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "g"}}, false});
    w.dependencies().setIdForTest(DependencySetId(20));
    w.dependencies().addDefForTest({WorkloadId(2), DependencyRequirement::REQUIRED, DependencyReady::COMPLETED, ""});
    w.dependencies().setStatus(0, DependencyStatus::SATISFIED, DependencyGeneration::fromU64(1));
    report("lifecycle.settle/readiness", nsPer(N, [&] { (void)w.settle(w.generation()); }));
  }

  // progress ingestion
  {
    Workload w = Workload::create(WorkloadId(1), "n", "i");
    w.setPolicyForTest(pol());
    w.startEpisode(ExecutionEpisodeId(1), OwnerId(1), OwnerGeneration::fromU64(1), true);
    w.admitDispatch(ExecutionEpisodeId(1), WorkerId(1), WorkerBootId(1));
    report("progress.ingestion", nsPer(N, [&] { (void)w.reportProgress(ExecutionEpisodeId(1), ExecutionEpisodeGeneration::fromU64(1), 1, true); }));
  }

  // resource-contract evaluation
  {
    Workload w = Workload::create(WorkloadId(1), "n", "i");
    w.contract().setIdForTest(ResourceContractId(10));
    w.contract().add({ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""});
    w.contract().applyGrant({ResourceContractId(10), w.contract().generation(), GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "g"}}, false});
    report("resource.contract.satisfied", nsPer(N, [&] { w.contract().satisfied(); }));
  }

  // priority evaluation
  {
    Priority p; p.setBase(PriorityClass(1), PriorityTier::NORMAL, 50); p.promote(7);
    report("priority.effective", nsPer(N, [&] { (void)p.effectiveValue(); }));
  }

  // episode creation
  {
    WorkloadEngine eng;
    (void)eng.create(WorkloadId::fromU64(1), "n", "i", pol());
    (void)eng.addResourceRequirement(WorkloadId::fromU64(1), {ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""});
    auto gen = eng.snapshot(WorkloadId::fromU64(1)).contract().generation();
    (void)eng.applyGrant(WorkloadId::fromU64(1), {ResourceContractId(10), gen, GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "g"}}, false});
    (void)eng.settle(WorkloadId::fromU64(1));
    report("episode.create", nsPer(N, [&] {
      auto r = eng.startEpisode(WorkloadId::fromU64(1), OwnerId(1), OwnerGeneration::fromU64(1));
      (void)r;
    }));
  }

  // restart planning
  {
    Workload w = Workload::create(WorkloadId(1), "n", "i");
    w.setPolicyForTest(pol());
    w.startEpisode(ExecutionEpisodeId(1), OwnerId(1), OwnerGeneration::fromU64(1), true);
    w.onWorkerLost(WorkerId(1), WorkerBootId(1), w.generation());
    report("restart.plan", nsPer(N, [&] {
      // note: consumes slots but bounded by maxRestarts; measure the rejection path
      (void)w.explainRestartAllowed();
    }));
  }

  // terminal transition
  {
    Workload w = Workload::create(WorkloadId(1), "n", "i");
    // measure a terminal transition rejection (already terminal) - cheap guard
    (void)w.transition(WorkloadState::COMPLETED, w.generation(), "x");
    report("lifecycle.terminal-guard", nsPer(N, [&] { (void)w.transition(WorkloadState::RUNNING, w.generation(), "x"); }));
  }

  // persistence save/recover
  {
    Workload w = Workload::create(WorkloadId(1), "n", "i");
    w.setPolicyForTest(pol());
    w.startEpisode(ExecutionEpisodeId(1), OwnerId(1), OwnerGeneration::fromU64(1), true);
    w.reportProgress(ExecutionEpisodeId(1), ExecutionEpisodeGeneration::fromU64(1), 5, true);
    auto bytes = encodeWorkload(w);
    report("persistence.encode", nsPer(N, [&] { (void)encodeWorkload(w); }));
    report("persistence.decode", nsPer(N, [&] { Outcome st; (void)decodeWorkload(bytes, st); }));
  }

  // protocol encode/decode
  {
    Message m; m.type = MsgType::DISPATCH; m.workloadId = 1; m.episodeId = 2; m.payload = "64:0:8";
    std::vector<std::uint8_t> f;
    report("protocol.encode", nsPer(N, [&] { encodeMessage(m, f); }));
    encodeMessage(m, f);
    report("protocol.decode", nsPer(N, [&] { (void)decodeMessage(f.data(), f.size()); }));
  }

  // concurrent workload mutation
  {
    WorkloadEngine eng;
    (void)eng.create(WorkloadId::fromU64(1), "n", "i", pol());
    auto t0 = Clock::now();
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) ts.emplace_back([&] {
      for (int i = 0; i < 5000; ++i) (void)eng.snapshot(WorkloadId::fromU64(1));
    });
    for (auto& th : ts) th.join();
    auto t2 = Clock::now();
    double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t0).count()) / 20000.0;
    report("concurrent.snapshot", ns);
  }

  std::printf("done\n");
  return 0;
}
