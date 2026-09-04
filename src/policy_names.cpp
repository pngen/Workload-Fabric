#include "workload_fabric/policy.hpp"
#include "workload_fabric/dependency.hpp"

namespace wf {
namespace {
inline constexpr const char* kRestart[] = {"NEVER","ON_KNOWN_FAILURE","ON_WORKER_LOSS","ON_INFRASTRUCTURE_FAILURE","ON_AMBIGUOUS_EXECUTION","ALWAYS_WITH_LIMIT","MANUAL_ONLY"};
inline constexpr const char* kCompletion[] = {"SINGLE_RESULT","ALL_PHASES","REQUIRED_UNITS","EXPLICIT_FINALIZE"};
inline constexpr const char* kMig[] = {"NONE","PLAN_ONLY","CHECKPOINT_AND_RESUME"};
inline constexpr const char* kCancelRace[] = {"CANCEL_FIRST","COMPLETION_FIRST"};
}  // namespace
std::string_view restartKindName(RestartKind k) noexcept { return static_cast<std::size_t>(k) < 7 ? kRestart[static_cast<std::size_t>(k)] : "INVALID"; }
std::string_view completionKindName(CompletionKind k) noexcept { return static_cast<std::size_t>(k) < 4 ? kCompletion[static_cast<std::size_t>(k)] : "INVALID"; }
std::string_view migrationEligibilityName(MigrationEligibility e) noexcept { return static_cast<std::size_t>(e) < 3 ? kMig[static_cast<std::size_t>(e)] : "INVALID"; }
std::string_view cancelRaceSemanticsName(CancelRaceSemantics s) noexcept { return static_cast<std::size_t>(s) < 2 ? kCancelRace[static_cast<std::size_t>(s)] : "INVALID"; }
}  // namespace wf
