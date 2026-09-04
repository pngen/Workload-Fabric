#include "test_framework.hpp"
#include "workload_fabric/coordinator.hpp"
#include "workload_fabric/transport.hpp"
#include "workload_fabric/protocol.hpp"
#include "workload_fabric/persistence.hpp"
#include "workload_fabric/worker.hpp"
#include <windows.h>
#include <process.h>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <cstdio>

using namespace wf;

static const char* g_workerExe = nullptr;

// ---- process spawn helper ------------------------------------------------
struct Proc { HANDLE h = nullptr; DWORD id = 0; std::string cmd; };
Proc spawnWorker(std::uint16_t port) {
  Proc p;
  std::string cmd = std::string("\"") + g_workerExe + "\" --port " + std::to_string(port);
  STARTUPINFOA si{}; si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<char> buf(cmd.begin(), cmd.end()); buf.push_back(0);
  if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { std::fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError()); return p; }
  p.h = pi.hProcess; p.id = pi.dwProcessId; p.cmd = cmd;
  CloseHandle(pi.hThread);
  return p;
}
void killProc(Proc& p) { if (p.h) { TerminateProcess(p.h, 1); WaitForSingleObject(p.h, 2000); CloseHandle(p.h); p.h = nullptr; } }

// ---- framing helpers ------------------------------------------------------
bool readMsg(FramePeer* peer, Message& m, int timeoutMs) { std::vector<std::uint8_t> f; if (!peer->receiveFrame(f, timeoutMs)) return false; try { m = decodeMessage(f.data(), f.size()); return true; } catch (ProtocolError const&) { return false; } }
bool sendMsg(FramePeer* peer, const Message& m) { std::vector<std::uint8_t> f; encodeMessage(m, f); return peer->sendFrame(f); }

static bool registerWorker(FramePeer* peer, std::uint64_t& outWorker, std::uint64_t& outBoot) {
  Message h;
  if (!readMsg(peer, h, 5000) || h.type != MsgType::HELLO) return false;
  outWorker = h.workerId; outBoot = h.bootId;
  Message reg; reg.type = MsgType::REGISTER; reg.workerId = h.workerId; reg.bootId = h.bootId;
  return sendMsg(peer, reg);
}

void testPrimaryScenario() {
  CASE("mp-primary");
  MemoryStateStore store;
  WorkloadEngine engine(&store);
  FrameListener* listener = createTcpListener(0);
  CHECK(listener != nullptr);
  if (!listener) return;
  std::uint16_t port = listener->boundPort();

  // policy: ALWAYS_WITH_LIMIT max=2, SINGLE_RESULT.
  Policy p; p.restart.kind = RestartKind::ALWAYS_WITH_LIMIT; p.restart.maxRestarts = 2;
  p.completion.kind = CompletionKind::SINGLE_RESULT;
  p.migration.eligibility = MigrationEligibility::NONE; p.suspension.allowed = true;
  p.generation = PolicyGeneration::fromU64(1);

  // create Workload W (id 1)
  CHECK(isAllow(engine.create(WorkloadId::fromU64(1), "W", "compute", p).outcome));
  CHECK(engine.addResourceRequirement(WorkloadId::fromU64(1), {ResourceKind::ACCELERATOR_COUNT, NeedLevel::REQUIRED, 1, 0, ""}).outcome == Outcome::ALLOW);
  auto gen = engine.snapshot(WorkloadId::fromU64(1)).contract().generation();
  CHECK(engine.applyGrant(WorkloadId::fromU64(1), {ResourceContractId::fromU64(10), gen, GrantStatus::GRANTED, {{ResourceKind::ACCELERATOR_COUNT, 1, "gpu0"}}, false}).outcome == Outcome::ALLOW);
  CHECK(isAllow(engine.validate(WorkloadId::fromU64(1)).outcome));
  CHECK(engine.settle(WorkloadId::fromU64(1)).outcome == Outcome::ALLOW);
  CHECK(engine.snapshot(WorkloadId::fromU64(1)).state() == WorkloadState::READY);
  WorkloadGeneration gen2 = engine.snapshot(WorkloadId::fromU64(1)).generation();

  // Spawn worker A, register.
  Proc A = spawnWorker(port);
  FramePeer* pa = listener->accept(10000);
  CHECK(pa != nullptr);
  std::uint64_t wa = 0, ba = 0;
  CHECK(pa && registerWorker(pa, wa, ba));
  CHECK(ba != 0);

  // start episode E1 -> dispatch to A.
  auto se1 = engine.startEpisode(WorkloadId::fromU64(1), OwnerId::fromU64(1), OwnerGeneration::fromU64(1));
  CHECK(se1.outcome == Outcome::ALLOW);
  auto ep1 = se1.episodeId.value();
  WorkloadGeneration gA = engine.snapshot(WorkloadId::fromU64(1)).generation();
  CHECK(engine.admitDispatch(WorkloadId::fromU64(1), ep1, WorkerId::fromU64(wa), WorkerBootId::fromU64(ba)).outcome == Outcome::ALLOW);

  Message d1; d1.type = MsgType::DISPATCH;
  d1.workloadId = 1; d1.episodeId = ep1.toU64(); d1.episodeGen = 1; d1.workloadGen = gA.toU64();
  d1.payload = "32:0:8"; d1.extra = "job-A";
  CHECK(sendMsg(pa, d1));

  // Read exactly two progress frames from A (16 units durable), then kill A. This
  // proves that a killed worker's partial progress becomes durable and can resume.
  std::uint64_t totalA = 0;
  for (int i = 0; i < 2; ++i) {
    Message m;
    CHECK(readMsg(pa, m, 5000));
    if (m.type != MsgType::PROGRESS) break;
    auto pr = engine.progress(WorkloadId::fromU64(1), ep1, ExecutionEpisodeGeneration::fromU64(1), m.value, m.flag1);
    CHECK(pr.outcome == Outcome::ALLOW);
    totalA += m.value;
  }
  CHECK(totalA == 16);
  CHECK(engine.snapshot(WorkloadId::fromU64(1)).progress().completed == 16);

  // Kill worker A as a real OS process; close peer.
  killProc(A);
  pa->close(); delete pa; pa = nullptr;
  // engine: worker lost -> recovering.
  CHECK(engine.onWorkerLost(WorkloadId::fromU64(1), WorkerId::fromU64(wa), WorkerBootId::fromU64(ba), gA).outcome == Outcome::ALLOW);
  CHECK(engine.snapshot(WorkloadId::fromU64(1)).state() == WorkloadState::RECOVERING);
  // plan restart.
  CHECK(engine.planRestart(WorkloadId::fromU64(1), "WORKER_LOSS", gA).outcome == Outcome::ALLOW);
  CHECK(engine.snapshot(WorkloadId::fromU64(1)).state() == WorkloadState::RESTART_PENDING);
  CHECK(engine.snapshot(WorkloadId::fromU64(1)).restartCount() == 1);

  // Spawn worker B, register (fresh boot).
  Proc B = spawnWorker(port);
  FramePeer* pb = listener->accept(10000);
  CHECK(pb != nullptr);
  std::uint64_t wb = 0, bb = 0;
  CHECK(pb && registerWorker(pb, wb, bb));
  CHECK(bb != ba);  // fresh WorkerBootId

  // startEpisode E2 (resumes from durable progress).
  auto se2 = engine.startEpisode(WorkloadId::fromU64(1), OwnerId::fromU64(2), OwnerGeneration::fromU64(1));
  CHECK(se2.outcome == Outcome::ALLOW);
  auto ep2 = se2.episodeId.value();
  CHECK(ep2 != ep1);
  WorkloadGeneration gB = engine.snapshot(WorkloadId::fromU64(1)).generation();
  CHECK(engine.admitDispatch(WorkloadId::fromU64(1), ep2, WorkerId::fromU64(wb), WorkerBootId::fromU64(bb)).outcome == Outcome::ALLOW);

  // Dispatch E2 to B.
  Message d2; d2.type = MsgType::DISPATCH;
  d2.workloadId = 1; d2.episodeId = ep2.toU64(); d2.episodeGen = 1; d2.workloadGen = gB.toU64();
  d2.payload = "32:16:8"; d2.extra = "job-B";  // resume from durable progress 16
  CHECK(sendMsg(pb, d2));

  // Read progress from B and feed engine until COMPLETE.
  bool gotComplete = false;
  for (int i = 0; i < 10 && !gotComplete; ++i) {
    Message m;
    if (!readMsg(pb, m, 5000)) break;
    if (m.type == MsgType::COMPLETE) { gotComplete = true; break; }
    if (m.type != MsgType::PROGRESS) continue;
    auto pr = engine.progress(WorkloadId::fromU64(1), ep2, ExecutionEpisodeGeneration::fromU64(1), m.value, m.flag1);
    CHECK(pr.outcome == Outcome::ALLOW);
  }
  CHECK(gotComplete);
  // The resumed episode continued from durable progress to the full total.
  CHECK(engine.snapshot(WorkloadId::fromU64(1)).progress().completed == 32);
  // Stale progress from the OLD episode E1 must be rejected.
  auto stale = engine.progress(WorkloadId::fromU64(1), ep1, ExecutionEpisodeGeneration::fromU64(1), 999, true);
  CHECK(stale.outcome == Outcome::REJECT_EPISODE_INELIGIBLE);  // E1 no longer authoritative

  // Mark episode completed, commit completion.
  CHECK(engine.episodeCompleted(WorkloadId::fromU64(1), ep2).outcome == Outcome::ALLOW);
  CHECK(engine.commitCompletion(WorkloadId::fromU64(1), gB).outcome == Outcome::ALLOW);
  CHECK(engine.snapshot(WorkloadId::fromU64(1)).state() == WorkloadState::COMPLETED);

  // exactly one terminal outcome; persist + reconstruct.
  auto wl = engine.snapshot(WorkloadId::fromU64(1));
  CHECK(wl.state() == WorkloadState::COMPLETED);
  auto loaded = store.load(WorkloadId::fromU64(1));
  CHECK(loaded.outcome == Outcome::ALLOW && loaded.workload.has_value());
  CHECK(loaded.workload->state() == WorkloadState::COMPLETED);

  // shutdown workers.
  Message sd; sd.type = MsgType::SHUTDOWN;
  if (pb) { sendMsg(pb, sd); pb->close(); delete pb; }
  if (pa) { pa->close(); delete pa; }
  killProc(B); killProc(A);
  listener->close(); delete listener;
}

int main(int argc, char** argv) {
  g_workerExe = (argc > 1) ? argv[1] : nullptr;
  if (!g_workerExe) { std::fprintf(stderr, "usage: mp_proof <worker_exe>\n"); return 2; }
  testPrimaryScenario();
  TEST_MAIN_END();
}
