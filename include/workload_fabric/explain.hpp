#pragma once
// Deterministic explanations. Every decision/transition carries a structured,
// serializable explanation so a caller can ask *why* and get an unambiguous answer.
#include <string>
#include <cstdint>
#include "workload_fabric/outcome.hpp"
#include "workload_fabric/state.hpp"
#include "workload_fabric/identity.hpp"

namespace wf {

struct Explanation {
  Outcome outcome = Outcome::UNKNOWN;
  WorkloadState from = WorkloadState::CREATED;
  WorkloadState to = WorkloadState::CREATED;
  WorkloadGeneration workloadGeneration;
  std::string detail;   // human/machine-readable, deterministic
};

std::string explainToString(const Explanation& e);

}  // namespace wf
