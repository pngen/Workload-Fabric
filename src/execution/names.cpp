#include "workload_fabric/execution.hpp"

namespace wf {
namespace {
inline constexpr const char* kExSt[] = {"DISPATCHED","RUNNING","SUSPENDED","PREEMPTED","CANCELLED","COMPLETED","FAILED","LOST","UNKNOWN"};
}  // namespace
std::string_view executionStateName(ExecutionState s) noexcept {
  auto i = static_cast<std::size_t>(s);
  return i < 9 ? kExSt[i] : "INVALID";
}
}  // namespace wf
