#include "workload_fabric/persistence.hpp"
#include "workload_fabric/state.hpp"
#include "workload_fabric/explain.hpp"
#include <cstdio>
#include <cstring>
#include <string>

using namespace wf;

// A small CLI inspection tool: decode a persisted workload record and print its
// durable state, generations, lifecycle history, and deterministic explanations.
// Usage: wf_inspect <workload-file.bin>
int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: wf_inspect <workload-file.bin>\n"); return 2; }
  std::FILE* f = std::fopen(argv[1], "rb");
  if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
  std::fseek(f, 0, SEEK_END);
  long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (sz < 0) { std::fclose(f); return 1; }
  std::uint8_t* buf = new std::uint8_t[static_cast<std::size_t>(sz) + 1];
  std::size_t got = std::fread(buf, 1, static_cast<std::size_t>(sz), f);
  std::fclose(f);
  std::vector<std::uint8_t> data(buf, buf + got);
  delete[] buf;

  Outcome status = Outcome::UNKNOWN;
  Workload w = decodeWorkload(data, status);
  if (status != Outcome::ALLOW) {
    std::printf("decode failed: %s\n", std::string(outcomeName(status)).c_str());
    return 1;
  }

  std::printf("Workload %llu '%s'\n", (unsigned long long)w.id().toU64(), w.name().c_str());
  std::printf("  state          = %s\n", std::string(workloadStateName(w.state())).c_str());
  std::printf("  generation     = %llu\n", (unsigned long long)w.generation().toU64());
  std::printf("  revision       = %llu\n", (unsigned long long)w.revision().toU64());
  std::printf("  coordinatorEp  = %llu\n", (unsigned long long)w.epoch().toU64());
  auto pct = w.progress().percent();
  std::printf("  progress       = %llu/%llu (%s)\n", (unsigned long long)w.progress().completed, (unsigned long long)w.progress().total, pct ? "known" : "unknown-total");
  std::printf("  restartCount   = %u (max %u)\n", w.restartCount(), w.policy().restart.maxRestarts);
  std::printf("  contractGen    = %llu (grant=%s)\n", (unsigned long long)w.contract().generation().toU64(), std::string(grantStatusName(w.contract().grant().status)).c_str());
  std::printf("  priority       = base %d + promo %d\n", w.priority().baseValue(), w.priority().promotion());
  std::printf("  episodes       = %zu\n", w.episodes().size());
  for (std::size_t i = 0; i < w.episodes().size(); ++i) {
    const auto& e = w.episodes()[i];
    std::printf("    [%zu] id=%llu gen=%llu state=%s auth=%d worker=%llu/boot=%llu\n", i, (unsigned long long)e.id.toU64(), (unsigned long long)e.generation.toU64(), std::string(episodeStateName(e.state)).c_str(), (int)e.authoritative, (unsigned long long)e.worker.toU64(), (unsigned long long)e.boot.toU64());
  }
  std::printf("  history        = %zu transitions\n", w.history().size());
  for (const auto& h : w.history()) {
    std::printf("    %s -> %s [%s] %s\n", std::string(workloadStateName(h.from)).c_str(), std::string(workloadStateName(h.to)).c_str(), std::string(outcomeName(h.outcome)).c_str(), h.reason.c_str());
  }
  auto exb = w.explainBlocked();
  std::printf("  why-blocked    = %s%s\n", std::string(outcomeName(exb.outcome)).c_str(), exb.detail.empty() ? "" : (": " + exb.detail).c_str());
  auto exr = w.explainRestartAllowed();
  std::printf("  why-restart    = %s%s\n", std::string(outcomeName(exr.outcome)).c_str(), exr.detail.empty() ? "" : (": " + exr.detail).c_str());
  return 0;
}
