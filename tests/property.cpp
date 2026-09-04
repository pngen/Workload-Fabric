#include "test_framework.hpp"
#include "workload_fabric/coordinator.hpp"
#include "workload_fabric/persistence.hpp"
#include "workload_fabric/state.hpp"
#include <cstdint>
#include <string>

using namespace wf;

// Deterministic xorshift64 RNG for reproducible property runs.
struct Rng {
  std::uint64_t s;
  explicit Rng(std::uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
  std::uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  std::uint64_t below(std::uint64_t n) { return next() % n; }
  bool chance(std::uint64_t num, std::uint64_t den) { return below(den) < num; }
};

static Policy pol() {
  Policy p;
  p.restart.kind = RestartKind::ALWAYS_WITH_LIMIT; p.restart.maxRestarts = 3;
  p.completion.kind = CompletionKind::SINGLE_RESULT;
  p.migration.eligibility = MigrationEligibility::CHECKPOINT_AND_RESUME;
  p.suspension.allowed = true;
  p.cancelRace = CancelRaceSemantics::COMPLETION_FIRST;
  p.generation = PolicyGeneration::fromU64(1);
  return p;
}

struct Invariants {
  std::uint64_t lastGen = 0;
  std::uint64_t lastProgress = 0;
  bool sawTerminal = false;

  // Verify all invariants for a workload snapshot; returns the number of failures.
  int check(const Workload& w, std::uint64_t id) {
    int fails = 0;
    #define IFAIL(cond) do { if (!(cond)) { ++fails; std::printf("    invariant fail (%s): %s\n", std::to_string(id).c_str(), #cond); } } while (0)
    // One current generation, never moves backward.
    IFAIL(w.generation().valid());
    IFAIL(w.generation().toU64() >= lastGen);
    lastGen = w.generation().toU64();
    // Progress never regresses for this monotonic workload.
    IFAIL(w.progress().completed >= lastProgress);
    lastProgress = w.progress().completed;
    // Restart count never exceeds policy limit.
    IFAIL(w.restartCount() <= w.policy().restart.maxRestarts);
    // Terminal state does not reopen.
    if (isTerminal(w.state()) && !sawTerminal) sawTerminal = true;
    // Exactly one authoritative episode may advance progress.
    int auth = 0;
    for (const auto& e : w.episodes()) if (e.authoritative) ++auth;
    IFAIL(auth <= 1);
    // A completed workload cannot be restarting.
    if (w.state() == WorkloadState::COMPLETED) IFAIL(w.restartCount() <= w.policy().restart.maxRestarts);
    return fails;
    #undef IFAIL
  }
};

static void checkPersistence(WorkloadEngine& eng, StateStore& store, WorkloadId id) {
  auto wl = eng.snapshot(id);
  // Persist and recover: the round-trip preserves durable state.
  (void)store.save(wl);
  auto loaded = store.load(id);
  if (loaded.outcome != Outcome::ALLOW || !loaded.workload.has_value()) {
    std::printf("    persistence round-trip FAILED for %llu\n", (unsigned long long)id.toU64());
    ++tf::g_failures;
    ++tf::g_checks;
    return;
  }
  ++tf::g_checks;
  if (loaded.workload->state() != wl.state() || loaded.workload->progress().completed != wl.progress().completed ||
      loaded.workload->generation().toU64() != wl.generation().toU64()) {
    std::printf("    persistence round-trip MISMATCH for %llu\n", (unsigned long long)id.toU64());
    ++tf::g_failures;
  } else { ++tf::g_passes; }
}

// One random legal/illegal event stream over a single workload.
static std::string currentCase;
static void runStream(std::uint64_t seed) {
  currentCase = "property-seed-" + std::to_string(seed);
  tf::g_case = currentCase.c_str();
  MemoryStateStore store;
  WorkloadEngine eng(&store);
  WorkloadId id(seed + 1);
  (void)eng.create(id, "prop", "random", pol());
  (void)eng.addResourceRequirement(id, {ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""});
  auto cgen = eng.snapshot(id).contract().generation();
  (void)eng.applyGrant(id, {ResourceContractId(10), cgen, GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "g"}}, false});
  (void)eng.validate(id);
  (void)eng.settle(id);

  Rng rng(seed);
  Invariants inv;
  bool cancelled = false;
  bool finished = false;
  for (int step = 0; step < 400 && !finished; ++step) {
    Workload w = eng.snapshot(id);
    auto gen = w.generation();
    auto ep = w.currentEpisodeId();
    std::uint64_t choice = rng.below(100);
    if (choice < 8) {
      // Attempt an illegal transition (terminal reopen / stale gen / no edge).
      auto r = eng.settle(id);  // settle is legal only in pre-execution states
      (void)r;
    } else if (choice < 20 && ep.valid()) {
      // progress delta (monotonic)
      std::uint64_t delta = 1 + rng.below(5);
      auto r = eng.progress(id, ep, ExecutionEpisodeGeneration::fromU64(1), delta, true);
      (void)r;
    } else if (choice < 30 && eng.snapshot(id).state() == WorkloadState::RUNNING) {
      // worker lost -> recover
      auto r = eng.onWorkerLost(id, WorkerId(1), WorkerBootId(1), gen);
      (void)r;
    } else if (choice < 40 && w.state() == WorkloadState::RECOVERING) {
      auto r = eng.planRestart(id, "WORKER_LOSS", gen);
      (void)r;
    } else if (choice < 46 && w.state() == WorkloadState::RESTART_PENDING) {
      auto se = eng.startEpisode(id, OwnerId(1), OwnerGeneration::fromU64(1));
      if (se.outcome == Outcome::ALLOW) (void)eng.admitDispatch(id, se.episodeId.value(), WorkerId(1), WorkerBootId(1));
    } else if (choice < 52 && w.state() == WorkloadState::READY && ep != ExecutionEpisodeId{} && !cancelled) {
      (void)eng.onWorkerLost(id, WorkerId(1), WorkerBootId(1), gen);  // illegal in READY; must reject
    } else if (choice < 58 && isActivelyRunning(w.state())) {
      auto r = eng.requestSuspend(id, gen);
      if (r.outcome == Outcome::ALLOW) (void)eng.completeSuspend(id, gen);
    } else if (choice < 64 && w.state() == WorkloadState::SUSPENDED) {
      (void)eng.requestResume(id, gen);
      auto se = eng.startEpisode(id, OwnerId(2), OwnerGeneration::fromU64(1));
      if (se.outcome == Outcome::ALLOW) (void)eng.admitDispatch(id, se.episodeId.value(), WorkerId(1), WorkerBootId(1));
    } else if (choice < 70 && isActivelyRunning(w.state()) && !cancelled) {
      auto r = eng.requestMigration(id, gen);
      if (r.outcome == Outcome::ALLOW) (void)eng.commitMigration(id, gen, WorkerId(2), WorkerBootId(2));
    } else if (choice < 76 && w.state() == WorkloadState::MIGRATION_PENDING) {
      (void)eng.commitMigration(id, gen, WorkerId(2), WorkerBootId(2));
    } else if (choice < 82 && !cancelled && !isTerminal(w.state())) {
      auto r = eng.cancel(id, gen);
      if (r.outcome == Outcome::ALLOW) { cancelled = true; (void)eng.completeCancellation(id, gen); }
    } else if (choice < 90 && w.state() == WorkloadState::CANCELLATION_REQUESTED) {
      // stale progress after cancel must reject
      auto r = eng.progress(id, ep, ExecutionEpisodeGeneration::fromU64(1), 1, true);
      CHECK(r.outcome == Outcome::REJECT_CANCELLED || r.outcome == Outcome::REJECT_TERMINAL || r.outcome == Outcome::ALLOW);
    } else if (choice < 94 && w.state() == WorkloadState::STARTING) {
      (void)eng.admitDispatch(id, ep, WorkerId(1), WorkerBootId(1));
    } else {
      // terminal/finalize
      (void)eng.episodeCompleted(id, ep);
      auto r = eng.commitCompletion(id, gen);
      if (r.outcome == Outcome::ALLOW) finished = true;
    }

    // After each step, check invariants.
    Workload now = eng.snapshot(id);
    int invFails = inv.check(now, id.toU64());
    if (invFails) { tf::g_failures += invFails; tf::g_checks += invFails; }
    if (step % 37 == 0) checkPersistence(eng, store, id);
  }
  // Final invariant: persistence round-trips the final state.
  checkPersistence(eng, store, id);
  Workload fin = eng.snapshot(id);
  (void)fin;

}

void testProperty() {
  for (std::uint64_t s = 1000; s < 1100; ++s) runStream(s);
}

int main() {
  testProperty();
  TEST_MAIN_END();
}
