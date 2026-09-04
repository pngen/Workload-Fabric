#include "workload_fabric/explain.hpp"

namespace wf {
std::string explainToString(const Explanation& e) {
  std::string s = std::string(outcomeName(e.outcome)) + " state=" + std::string(workloadStateName(e.from));
  s += " -> " + std::string(workloadStateName(e.to));
  if (e.workloadGeneration.valid()) s += " wgen=" + std::to_string(e.workloadGeneration.toU64());
  if (!e.detail.empty()) { s += " "; s += e.detail; }
  return s;
}
}  // namespace wf
