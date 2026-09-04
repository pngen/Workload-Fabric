#include <workload_fabric/workload.hpp>
#include <workload_fabric/state.hpp>
#include <cstdio>
// Downstream consumer: uses find_package(WorkloadFabric CONFIG REQUIRED) and links
// the exported WorkloadFabric::WorkloadFabric target. This validates the installable
// CMake package.
int main() {
  wf::Workload w = wf::Workload::create(wf::WorkloadId(7), "consumer", "downstream");
  wf::Policy p;
  p.completion.kind = wf::CompletionKind::SINGLE_RESULT;
  p.restart.kind = wf::RestartKind::ALWAYS_WITH_LIMIT;
  p.restart.maxRestarts = 1;
  p.generation = wf::PolicyGeneration::fromU64(1);
  w.setPolicyForTest(p);
  std::printf("consumer ok: workload=%llu state=%s\n", (unsigned long long)w.id().toU64(), std::string(wf::workloadStateName(w.state())).c_str());
  return 0;
}
