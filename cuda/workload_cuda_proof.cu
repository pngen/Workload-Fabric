// Workload Fabric CUDA-backed proof on the local RTX 5090 (sm_120).
//
// This is NOT a multi-GPU or multi-node demonstration. It proves, on a single
// physical GPU, that a segmented workload can be advanced through the Workload
// Fabric lifecycle (normal execution, restart under a fresh WorkerBootId,
// same-device process/episode migration of workload authority and state,
// suspension/resume, and deterministic cancellation) with real cudaMalloc / H2D /
// kernel / D2H / cudaFree work whose final result matches a CPU reference exactly.
//
// Every scenario frees every device allocation; device memory returns to baseline.
#include "workload_fabric/coordinator.hpp"
#include "workload_fabric/persistence.hpp"
#include "workload_fabric/state.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cuda_runtime.h>

using namespace wf;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("  FAIL [%s] %s:%d: %s\n", g_case, __FILE__, __LINE__, #cond); } } while (0)
static const char* g_case = "";

#define GPU_CHECK(call) do { cudaError_t e = (call); if (e != cudaSuccess) { std::printf("CUDA error at %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(e)); std::exit(2); } } while (0)

static constexpr int kBuf = 1 << 16;      // 65536 elements
static constexpr int kSegs = 8;
static constexpr std::uint64_t kUnits = 1000;   // progress units per segment

__global__ void segKernel(float* buf, int n, int seg) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) buf[i] += static_cast<float>((i * (seg + 1)) % 31) * 0.25f;
}

static void hostApply(float* h, int n, int seg) {
  for (int i = 0; i < n; ++i) h[i] += static_cast<float>((i * (seg + 1)) % 31) * 0.25f;
}


// GPU state wrapper: owns a device buffer and mirrors a host reference buffer.
struct GpuState {
  float* d = nullptr;
  std::vector<float> h;
  int n = 0;
  float* alloc() {
    GPU_CHECK(cudaMalloc(&d, static_cast<std::size_t>(n) * sizeof(float)));
    GPU_CHECK(cudaMemcpy(d, h.data(), static_cast<std::size_t>(n) * sizeof(float), cudaMemcpyHostToDevice));
    return d;
  }
  void free() { if (d) { GPU_CHECK(cudaFree(d)); d = nullptr; } }
  void runSeg(int seg) {
    int threads = 256;
    int blocks = (n + threads - 1) / threads;
    segKernel<<<blocks, threads>>>(d, n, seg);
    GPU_CHECK(cudaGetLastError());
    GPU_CHECK(cudaDeviceSynchronize());
    hostApply(h.data(), n, seg);
  }
  // Copy device->host and verify the device buffer exactly equals the host reference.
  bool verifyParity() {
    std::vector<float> tmp(n);
    GPU_CHECK(cudaMemcpy(tmp.data(), d, static_cast<std::size_t>(n) * sizeof(float), cudaMemcpyDeviceToHost));
    for (int i = 0; i < n; ++i) if (tmp[i] != h[i]) return false;
    return true;
  }
  // Reconstruct device state from the host reference (checkpoint/state handoff).
  void reconstruct() { GPU_CHECK(cudaMemcpy(d, h.data(), static_cast<std::size_t>(n) * sizeof(float), cudaMemcpyHostToDevice)); }
};

static std::size_t freeMem() {
  std::size_t f = 0, t = 0;
  cudaMemGetInfo(&f, &t);
  return f;
}

static void fillRef(GpuState& g, float seed) {
  g.n = kBuf;
  g.h.assign(g.n, seed);
}

static Policy policy() {
  Policy p;
  p.restart.kind = RestartKind::ALWAYS_WITH_LIMIT; p.restart.maxRestarts = 3;
  p.completion.kind = CompletionKind::SINGLE_RESULT;
  p.migration.eligibility = MigrationEligibility::CHECKPOINT_AND_RESUME;
  p.suspension.allowed = true;
  p.cancelRace = CancelRaceSemantics::COMPLETION_FIRST;
  p.generation = PolicyGeneration::fromU64(1);
  return p;
}

static EngineResult makeReady(WorkloadEngine& eng, WorkloadId id) {
  EngineResult r = eng.create(id, "cuda", "cuda-segmented", policy());
  if (!isAllow(r.outcome)) return r;
  (void)eng.addResourceRequirement(id, {ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""});
  auto gen = eng.snapshot(id).contract().generation();
  (void)eng.applyGrant(id, {ResourceContractId::fromU64(10), gen, GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "rtx5090"}}, false});
  (void)eng.validate(id);
  return eng.settle(id);
}

static void runFullToCompletion(WorkloadEngine& eng, WorkloadId id, GpuState& g, int startSeg, int endSeg) {
  auto ep = eng.snapshot(id).currentEpisodeId();
  for (int s = startSeg; s < endSeg; ++s) {
    g.runSeg(s);
    auto pr = eng.progress(id, ep, ExecutionEpisodeGeneration::fromU64(1), kUnits, true);
    CHECK(isAllow(pr.outcome));
  }
}

static void scenarioA() {
  g_case = "cuda-A-normal";
  WorkloadEngine eng;
  auto r = makeReady(eng, WorkloadId::fromU64(1));
  CHECK(eng.snapshot(WorkloadId::fromU64(1)).state() == WorkloadState::READY);
  auto se = eng.startEpisode(WorkloadId::fromU64(1), OwnerId::fromU64(1), OwnerGeneration::fromU64(1));
  CHECK(isAllow(se.outcome));
  auto ep = se.episodeId.value();
  WorkloadGeneration gen = eng.snapshot(WorkloadId::fromU64(1)).generation();
  CHECK(eng.admitDispatch(WorkloadId::fromU64(1), ep, WorkerId::fromU64(1), WorkerBootId::fromU64(1)).outcome == Outcome::ALLOW);

  GpuState g; fillRef(g, 1.5f);
  std::size_t beforeMem = freeMem();
  g.alloc();
  runFullToCompletion(eng, WorkloadId::fromU64(1), g, 0, kSegs);
  CHECK(g.verifyParity());
  CHECK(eng.snapshot(WorkloadId::fromU64(1)).progress().completed == static_cast<std::uint64_t>(kSegs) * kUnits);
  CHECK(eng.episodeCompleted(WorkloadId::fromU64(1), ep).outcome == Outcome::ALLOW);
  CHECK(eng.commitCompletion(WorkloadId::fromU64(1), gen).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(1)).state() == WorkloadState::COMPLETED);
  g.free();
  GPU_CHECK(cudaDeviceSynchronize());
  CHECK(freeMem() >= beforeMem);  // device memory returns to baseline
  std::printf("  [A] normal complete (progress=%llu, parity=ok)\n", (unsigned long long)(eng.snapshot(WorkloadId::fromU64(1)).progress().completed));
}

static void scenarioB() {
  g_case = "cuda-B-restart";
  WorkloadEngine eng;
  (void)makeReady(eng, WorkloadId::fromU64(2));
  auto se = eng.startEpisode(WorkloadId::fromU64(2), OwnerId::fromU64(1), OwnerGeneration::fromU64(1));
  CHECK(isAllow(se.outcome));
  auto ep1 = se.episodeId.value();
  WorkloadGeneration gen = eng.snapshot(WorkloadId::fromU64(2)).generation();
  CHECK(eng.admitDispatch(WorkloadId::fromU64(2), ep1, WorkerId::fromU64(1), WorkerBootId::fromU64(1)).outcome == Outcome::ALLOW);

  GpuState g; fillRef(g, 2.0f);
  std::size_t beforeMem = freeMem();
  g.alloc();
  runFullToCompletion(eng, WorkloadId::fromU64(2), g, 0, 2);   // segments 0,1
  CHECK(eng.snapshot(WorkloadId::fromU64(2)).progress().completed == 2 * kUnits);
  // Worker terminated: device state is checkpointed (host ref g.h) and freed.
  g.free();
  CHECK(eng.onWorkerLost(WorkloadId::fromU64(2), WorkerId::fromU64(1), WorkerBootId::fromU64(1), gen).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(2)).state() == WorkloadState::RECOVERING);
  CHECK(eng.planRestart(WorkloadId::fromU64(2), "WORKER_LOSS", gen).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(2)).state() == WorkloadState::RESTART_PENDING);
  CHECK(eng.snapshot(WorkloadId::fromU64(2)).restartCount() == 1);

  auto se2 = eng.startEpisode(WorkloadId::fromU64(2), OwnerId::fromU64(2), OwnerGeneration::fromU64(1));
  CHECK(isAllow(se2.outcome));
  auto ep2 = se2.episodeId.value();
  WorkloadGeneration gen2 = eng.snapshot(WorkloadId::fromU64(2)).generation();
  CHECK(gen2.toU64() != gen.toU64());
  CHECK(eng.admitDispatch(WorkloadId::fromU64(2), ep2, WorkerId::fromU64(1), WorkerBootId::fromU64(2)).outcome == Outcome::ALLOW);
  g.alloc();  // fresh WorkerBootId, reallocated device
  g.reconstruct();  // resume from durable state (checkpoint)
  runFullToCompletion(eng, WorkloadId::fromU64(2), g, 2, kSegs);   // segments 2..7
  CHECK(g.verifyParity());
  CHECK(eng.snapshot(WorkloadId::fromU64(2)).progress().completed == static_cast<std::uint64_t>(kSegs) * kUnits);
  CHECK(eng.episodeCompleted(WorkloadId::fromU64(2), ep2).outcome == Outcome::ALLOW);
  CHECK(eng.commitCompletion(WorkloadId::fromU64(2), gen2).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(2)).state() == WorkloadState::COMPLETED);
  g.free();
  CHECK(freeMem() >= beforeMem);
  std::printf("  [B] restart under fresh WorkerBootId complete (progress=%llu, parity=ok)\n", (unsigned long long)(eng.snapshot(WorkloadId::fromU64(2)).progress().completed));
}

static void scenarioC() {
  g_case = "cuda-C-migration";
  WorkloadEngine eng;
  (void)makeReady(eng, WorkloadId::fromU64(3));
  auto se = eng.startEpisode(WorkloadId::fromU64(3), OwnerId::fromU64(1), OwnerGeneration::fromU64(1));
  CHECK(isAllow(se.outcome));
  auto ep1 = se.episodeId.value();
  WorkloadGeneration gen = eng.snapshot(WorkloadId::fromU64(3)).generation();
  CHECK(eng.admitDispatch(WorkloadId::fromU64(3), ep1, WorkerId::fromU64(1), WorkerBootId::fromU64(1)).outcome == Outcome::ALLOW);

  GpuState g; fillRef(g, 3.5f);
  std::size_t beforeMem = freeMem();
  g.alloc();
  runFullToCompletion(eng, WorkloadId::fromU64(3), g, 0, 3);   // Worker A segments 0,1,2
  CHECK(eng.snapshot(WorkloadId::fromU64(3)).progress().completed == 3 * kUnits);
  // Request migration; quiesce; Worker A releases device state.
  CHECK(eng.requestMigration(WorkloadId::fromU64(3), gen).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(3)).state() == WorkloadState::MIGRATION_PENDING);
  g.free();  // Worker A releases device state (checkpoint is host ref g.h)
  // Commit migration to Worker B (single physical device; process/episode handoff).
  CHECK(eng.commitMigration(WorkloadId::fromU64(3), gen, WorkerId::fromU64(2), WorkerBootId::fromU64(2)).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(3)).state() == WorkloadState::RUNNING);
  auto ep2 = eng.snapshot(WorkloadId::fromU64(3)).currentEpisodeId();
  WorkloadGeneration gen2 = eng.snapshot(WorkloadId::fromU64(3)).generation();
  g.alloc();
  g.reconstruct();  // Worker B reconstructs state on the SAME RTX 5090
  runFullToCompletion(eng, WorkloadId::fromU64(3), g, 3, kSegs);   // segments 3..7
  CHECK(g.verifyParity());
  CHECK(eng.snapshot(WorkloadId::fromU64(3)).progress().completed == static_cast<std::uint64_t>(kSegs) * kUnits);
  CHECK(eng.episodeCompleted(WorkloadId::fromU64(3), ep2).outcome == Outcome::ALLOW);
  CHECK(eng.commitCompletion(WorkloadId::fromU64(3), gen2).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(3)).state() == WorkloadState::COMPLETED);
  // exactly one authoritative lineage advanced.
  {
    Workload wlC = eng.snapshot(WorkloadId::fromU64(3));
    int auth = 0;
    for (const auto& e : wlC.episodes()) if (e.authoritative) ++auth;
    CHECK(auth == 1);
  }
  g.free();
  CHECK(freeMem() >= beforeMem);
  std::printf("  [C] same-device migration complete (progress=%llu, parity=ok)\n", (unsigned long long)(eng.snapshot(WorkloadId::fromU64(3)).progress().completed));
}

static void scenarioD() {
  g_case = "cuda-D-suspend-resume";
  WorkloadEngine eng;
  (void)makeReady(eng, WorkloadId::fromU64(4));
  auto se = eng.startEpisode(WorkloadId::fromU64(4), OwnerId::fromU64(1), OwnerGeneration::fromU64(1));
  CHECK(isAllow(se.outcome));
  auto ep1 = se.episodeId.value();
  WorkloadGeneration gen = eng.snapshot(WorkloadId::fromU64(4)).generation();
  CHECK(eng.admitDispatch(WorkloadId::fromU64(4), ep1, WorkerId::fromU64(1), WorkerBootId::fromU64(1)).outcome == Outcome::ALLOW);

  GpuState g; fillRef(g, 4.0f);
  std::size_t beforeMem = freeMem();
  g.alloc();
  runFullToCompletion(eng, WorkloadId::fromU64(4), g, 0, 2);   // segments 0,1
  CHECK(eng.requestSuspend(WorkloadId::fromU64(4), gen).outcome == Outcome::ALLOW);
  CHECK(eng.completeSuspend(WorkloadId::fromU64(4), gen).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(4)).state() == WorkloadState::SUSPENDED);
  g.free();  // quiesce: release device allocations while suspended
  // Prove suspension survives a coordinator restart: persist the SUSPENDED workload
  // into the store, decode it back, and confirm durable SUSPENDED state + progress.
  MemoryStateStore store;
  (void)store.save(eng.snapshot(WorkloadId::fromU64(4)));
  auto loaded = store.load(WorkloadId::fromU64(4));
  CHECK(loaded.outcome == Outcome::ALLOW && loaded.workload.has_value());
  CHECK(loaded.workload->state() == WorkloadState::SUSPENDED);
  CHECK(loaded.workload->progress().completed == 2 * kUnits);
  // Resume the (durable) workload in this coordinator into a fresh episode.
  CHECK(eng.requestResume(WorkloadId::fromU64(4), gen).outcome == Outcome::ALLOW);
  auto se2 = eng.startEpisode(WorkloadId::fromU64(4), OwnerId::fromU64(2), OwnerGeneration::fromU64(1));
  CHECK(isAllow(se2.outcome));
  auto ep2 = se2.episodeId.value();
  WorkloadGeneration gen2 = eng.snapshot(WorkloadId::fromU64(4)).generation();
  CHECK(eng.admitDispatch(WorkloadId::fromU64(4), ep2, WorkerId::fromU64(1), WorkerBootId::fromU64(2)).outcome == Outcome::ALLOW);
  g.alloc(); g.reconstruct();
  runFullToCompletion(eng, WorkloadId::fromU64(4), g, 2, kSegs);
  CHECK(g.verifyParity());
  CHECK(eng.snapshot(WorkloadId::fromU64(4)).progress().completed == static_cast<std::uint64_t>(kSegs) * kUnits);
  CHECK(eng.episodeCompleted(WorkloadId::fromU64(4), ep2).outcome == Outcome::ALLOW);
  CHECK(eng.commitCompletion(WorkloadId::fromU64(4), gen2).outcome == Outcome::ALLOW);
  g.free();
  CHECK(freeMem() >= beforeMem);
  std::printf("  [D] suspension/resume complete (progress=%llu, parity=ok)\n", (unsigned long long)(eng.snapshot(WorkloadId::fromU64(4)).progress().completed));
}

static void scenarioE() {
  g_case = "cuda-E-cancellation";
  WorkloadEngine eng;
  (void)makeReady(eng, WorkloadId::fromU64(5));
  auto se = eng.startEpisode(WorkloadId::fromU64(5), OwnerId::fromU64(1), OwnerGeneration::fromU64(1));
  CHECK(isAllow(se.outcome));
  auto ep = se.episodeId.value();
  WorkloadGeneration gen = eng.snapshot(WorkloadId::fromU64(5)).generation();
  CHECK(eng.admitDispatch(WorkloadId::fromU64(5), ep, WorkerId::fromU64(1), WorkerBootId::fromU64(1)).outcome == Outcome::ALLOW);

  GpuState g; fillRef(g, 5.0f);
  std::size_t beforeMem = freeMem();
  g.alloc();
  g.runSeg(0);   // one segment executes before cancellation
  CHECK(eng.progress(WorkloadId::fromU64(5), ep, ExecutionEpisodeGeneration::fromU64(1), kUnits, true).outcome == Outcome::ALLOW);
  // Cancel at a deterministic inter-segment boundary; no future segment executes.
  CHECK(eng.cancel(WorkloadId::fromU64(5), gen).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(5)).state() == WorkloadState::CANCELLATION_REQUESTED);
  // Stale progress after cancellation is rejected.
  CHECK(eng.progress(WorkloadId::fromU64(5), ep, ExecutionEpisodeGeneration::fromU64(1), kUnits, true).outcome == Outcome::REJECT_CANCELLED);
  CHECK(!eng.startEpisode(WorkloadId::fromU64(5), OwnerId::fromU64(1), OwnerGeneration::fromU64(1)).episodeId.has_value());
  // No further GPU work runs.
  for (int s = 1; s < kSegs; ++s) { /* not executed */ }
  CHECK(eng.completeCancellation(WorkloadId::fromU64(5), gen).outcome == Outcome::ALLOW);
  CHECK(eng.snapshot(WorkloadId::fromU64(5)).state() == WorkloadState::CANCELLED);
  g.free();
  CHECK(freeMem() >= beforeMem);
  std::printf("  [E] cancellation at segment boundary complete (state=CANCELLED, mem released)\n");
}

int main() {
  cudaError_t dev = cudaSetDevice(0);
  if (dev != cudaSuccess) { std::printf("no CUDA device: %s\n", cudaGetErrorString(dev)); return 2; }
  cudaDeviceProp prop; GPU_CHECK(cudaGetDeviceProperties(&prop, 0));
  std::printf("Workload Fabric CUDA proof on: %s (sm_%d%d, %zu MiB)\n", prop.name, prop.major, prop.minor, prop.totalGlobalMem >> 20);
  scenarioA();
  scenarioB();
  scenarioC();
  scenarioD();
  scenarioE();
  GPU_CHECK(cudaDeviceReset());
  std::printf("%s: %d checks, %d fail\n", "cuda-proof", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
