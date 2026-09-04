#include "workload_fabric/state.hpp"
#include "workload_fabric/outcome.hpp"
#include "workload_fabric/identity.hpp"
#include "workload_fabric/version.hpp"
#include <cassert>
#include <iostream>
int main() {
  static_assert(wf::canTransition(wf::WorkloadState::RUNNING, wf::WorkloadState::COMPLETING));
  static_assert(!wf::canTransition(wf::WorkloadState::CANCELLED, wf::WorkloadState::RUNNING));
  static_assert(wf::canTransition(wf::WorkloadState::READY, wf::WorkloadState::CANCELLED));
  static_assert(wf::isTerminal(wf::WorkloadState::COMPLETED));
  wf::WorkloadId id(7);
  assert(id.valid());
  assert(wf::outcomeName(wf::Outcome::REJECT_STALE_BOOT) == "REJECT_STALE_BOOT");
  std::cout << "smoke ok " << wf::kVersion << "\n";
  return 0;
}
