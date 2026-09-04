#include "workload_fabric/worker.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
  std::uint16_t port = 0;
  for (int i = 1; i < argc; ++i) { if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = static_cast<std::uint16_t>(std::atoi(argv[i + 1])); }
  if (port == 0) { std::fprintf(stderr, "worker: --port required\n"); return 1; }
  wf::FramePeer* peer = wf::connectTcpPeer("127.0.0.1", port, 5000);
  if (!peer) { std::fprintf(stderr, "worker: connect failed\n"); return 1; }
  wf::WorkerId w = wf::freshWorkerId();
  wf::WorkerBootId b = wf::freshBootId();
  int rc = wf::runWorker(peer, wf::cpuReferenceSegment, w, b);
  delete peer;
  return rc;
}
