#include "workload_fabric/workload.hpp"
#include "workload_fabric/persistence.hpp"
#include <cstdio>

// Minimal example: drive one workload through its lifecycle using the public API.
using namespace wf;

int main() {
  Policy p;
  p.restart.kind = RestartKind::ALWAYS_WITH_LIMIT;
  p.restart.maxRestarts = 2;
  p.completion.kind = CompletionKind::SINGLE_RESULT;
  p.migration.eligibility = MigrationEligibility::CHECKPOINT_AND_RESUME;
  p.suspension.allowed = true;
  p.generation = PolicyGeneration::fromU64(1);

  Workload w = Workload::create(WorkloadId(42), "example", "compute 42");
  w.setPolicyForTest(p);

  // Resource contract: require one accelerator, then a grant binds it.
  w.contract().setIdForTest(ResourceContractId(10));
  w.contract().add({ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""});
  w.contract().applyGrant({ResourceContractId(10), w.contract().generation(), GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "gpu0"}}, false});

  // Dependency: require workload 1 to complete first, then satisfy it.
  w.dependencies().setIdForTest(DependencySetId(20));
  w.dependencies().addDefForTest({WorkloadId(1), DependencyRequirement::REQUIRED, DependencyReady::COMPLETED, ""});

  std::printf("state: %s\n", std::string(workloadStateName(w.state())).c_str());
  w.transition(WorkloadState::VALIDATING, w.generation(), "validate");
  w.settle(w.generation());
  std::printf("state: %s\n", std::string(workloadStateName(w.state())).c_str());
  std::printf("why-blocked: %s\n", std::string(outcomeName(w.explainBlocked().outcome)).c_str());

  // Satisfy the required dependency -> unblocks to READY.
  w.dependencies().setStatus(0, DependencyStatus::SATISFIED, DependencyGeneration::fromU64(1));
  w.settle(w.generation());
  std::printf("state: %s\n", std::string(workloadStateName(w.state())).c_str());

  // Start an episode, dispatch, advance progress, complete.
  w.startEpisode(ExecutionEpisodeId(100), OwnerId(1), OwnerGeneration::fromU64(1), true);
  auto gen = w.generation();
  w.admitDispatch(ExecutionEpisodeId(100), WorkerId(1), WorkerBootId(1));
  w.reportProgress(ExecutionEpisodeId(100), ExecutionEpisodeGeneration::fromU64(1), 7, true);
  w.markEpisodeCompleted(ExecutionEpisodeId(100));
  w.commitCompletion(gen);
  std::printf("state: %s (progress=%llu)\n", std::string(workloadStateName(w.state())).c_str(), (unsigned long long)w.progress().completed);

  // Persist and recover.
  auto bytes = encodeWorkload(w);
  Outcome st = Outcome::UNKNOWN;
  Workload r = decodeWorkload(bytes, st);
  std::printf("recovered: state=%s gen=%llu id=%llu\n", std::string(workloadStateName(r.state())).c_str(), (unsigned long long)r.generation().toU64(), (unsigned long long)r.id().toU64());
  // Also persist to a file so the wf_inspect tool can decode it.
  FILE* f = std::fopen("wf-example-1.bin", "wb");
  if (f) { std::fwrite(bytes.data(), 1, bytes.size(), f); std::fclose(f); std::printf("persisted to wf-example-1.bin (%zu bytes)\n", bytes.size()); }
  return 0;
}
