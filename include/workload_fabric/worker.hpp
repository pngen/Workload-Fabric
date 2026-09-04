#pragma once
// Worker-side runtime. A worker process connects to the coordinator over a framed
// transport, registers with a fresh WorkerBootId, and executes dispatched
// segmented work, reporting authoritative progress deltas per segment. The actual
// computation is injected as a SegmentRunner so the same worker runtime drives CPU
// reference work and CUDA-backed segmented work.
#include <cstdint>
#include <string>
#include <functional>
#include "workload_fabric/transport.hpp"
#include "workload_fabric/identity.hpp"

namespace wf {

// Work descriptor passed to the dispatch handler. The coordinator encodes the
// segmented work as "<totalUnits>:<resumeFrom>:<chunk>". resumeFrom is the durable
// progress a resumed episode starts from; the worker executes the remaining units
// in chunk-sized increments, reporting added progress for each.
struct WorkDescriptor {
  std::string raw;
  std::uint64_t total = 0;
  std::uint64_t resumeFrom = 0;
  std::uint64_t chunk = 0;
  std::string token;
  bool parse();
};

// Per-segment handler: performs the segment's work and returns the progress delta
// it authoritative-advances. The runtime sends a PROGRESS message per segment.
using SegmentRunner = std::function<std::uint64_t(std::uint64_t segment, std::uint64_t unitsPerSegment)>;

// A deterministic CPU reference runner for the multiprocess proof. Computes a
// reproducible checksum; the coordinator recomputes the same value for parity.
std::uint64_t cpuReferenceSegment(std::uint64_t segment, std::uint64_t unitsPerSegment);

// Run the worker loop on an established peer. Returns 0 on clean shutdown, non-zero
// on protocol error. WorkerId/BootId are supplied by the app (fresh per process).
int runWorker(FramePeer* peer, const SegmentRunner& runner, WorkerId workerId, WorkerBootId bootId);

// Generate a fresh WorkerBootId (process-lifetime, guaranteed distinct from any
// previously observed boot id in this address space).
WorkerBootId freshBootId();
WorkerId freshWorkerId();

}  // namespace wf
