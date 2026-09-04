#include "workload_fabric/worker.hpp"
#include "workload_fabric/protocol.hpp"
#include "workload_fabric/detail/binary.hpp"
#include <cstdlib>
#include <process.h>
#include <chrono>
#include <algorithm>
#include <vector>
#include <thread>
#include <atomic>

namespace wf {

bool WorkDescriptor::parse() {
  std::vector<std::string> parts;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= raw.size(); ++i) {
    if (i == raw.size() || raw[i] == ':') { parts.push_back(raw.substr(start, i - start)); start = i + 1; }
  }
  if (parts.size() != 3) return false;
  try {
    total = static_cast<std::uint64_t>(std::stoull(parts[0]));
    resumeFrom = static_cast<std::uint64_t>(std::stoull(parts[1]));
    chunk = static_cast<std::uint64_t>(std::stoull(parts[2]));
  } catch (...) { return false; }
  if (chunk == 0 || resumeFrom > total) return false;
  return true;
}

std::uint64_t cpuReferenceSegment(std::uint64_t segment, std::uint64_t unitsPerSegment) {
  // Deterministic, cheap, verifiable: a running sum seeded by the segment index.
  std::uint64_t acc = 0;
  for (std::uint64_t i = 0; i < unitsPerSegment; ++i) {
    acc += (segment + 1) * 31u + i * 7u + (i % 13u);
    acc ^= (acc >> 7);
  }
  return acc;
}

int runWorker(FramePeer* peer, const SegmentRunner& runner, WorkerId workerId, WorkerBootId bootId) {
  // HELLO: announce identity + fresh boot.
  {
    Message hello;
    hello.type = MsgType::HELLO;
    hello.workerId = workerId.toU64();
    hello.bootId = bootId.toU64();
    std::vector<std::uint8_t> frame;
    encodeMessage(hello, frame);
    if (!peer->sendFrame(frame)) return 2;
  }
  // Wait for REGISTER ack (or any frame; be tolerant of coordinator ordering).
  Message ack;
  std::vector<std::uint8_t> frame;
  if (!peer->receiveFrame(frame, 5000)) return 2;
  ack = decodeMessage(frame.data(), frame.size());

  while (true) {
    std::vector<std::uint8_t> f;
    if (!peer->receiveFrame(f, -1)) return 3;  // peer closed
    Message m;
    try { m = decodeMessage(f.data(), f.size()); }
    catch (ProtocolError const&) { continue; }
    if (m.type == MsgType::SHUTDOWN) return 0;
    if (m.type == MsgType::PING) {
      Message pong; pong.type = MsgType::PING;
      std::vector<std::uint8_t> fr; encodeMessage(pong, fr); peer->sendFrame(fr);
      continue;
    }
    if (m.type == MsgType::CANCEL) { return 4; }  // stop work
    if (m.type == MsgType::DISPATCH) {
      WorkDescriptor d; d.raw = m.payload; d.token = m.extra;
      if (!d.parse()) { return 5; }
      std::uint64_t remaining = d.total - d.resumeFrom;
      std::uint64_t reported = 0;
      std::uint64_t result = 0;
      while (reported < remaining) {
        std::uint64_t step = std::min<std::uint64_t>(d.chunk, remaining - reported);
        for (std::uint64_t u = 0; u < step; ++u) result += runner(d.resumeFrom + reported + u, d.total);
        reported += step;
        Message prog;
        prog.type = MsgType::PROGRESS;
        prog.workloadId = m.workloadId;
        prog.episodeId = m.episodeId;
        prog.episodeGen = m.episodeGen;
        prog.workloadGen = m.workloadGen;
        prog.workerId = workerId.toU64();
        prog.bootId = bootId.toU64();
        prog.value = step;       // units completed this increment
        prog.flag1 = true;       // durable
        std::vector<std::uint8_t> pf; encodeMessage(prog, pf);
        if (!peer->sendFrame(pf)) return 6;
      }
      Message done;
      done.type = MsgType::COMPLETE;
      done.workloadId = m.workloadId;
      done.episodeId = m.episodeId;
      done.episodeGen = m.episodeGen;
      done.workloadGen = m.workloadGen;
      done.workerId = workerId.toU64();
      done.bootId = bootId.toU64();
      done.payload = std::to_string(result);
      std::vector<std::uint8_t> df; encodeMessage(done, df);
      if (!peer->sendFrame(df)) return 7;
    }
  }
}

WorkerBootId freshBootId() {
  static std::atomic<std::uint64_t> ctr{1};
  auto now = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  auto pid = static_cast<std::uint64_t>(_getpid());
  std::uint64_t v = (now ^ (pid << 32) ^ (ctr.fetch_add(1) * 0x9E3779B97F4A7C15ULL)) & ~0ULL;
  if (v == 0) v = 1;
  return WorkerBootId::fromU64(v);
}

WorkerId freshWorkerId() {
  static std::atomic<std::uint64_t> ctr{1000};
  return WorkerId::fromU64(ctr.fetch_add(1));
}

}  // namespace wf
