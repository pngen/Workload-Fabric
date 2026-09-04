#include "workload_fabric/progress.hpp"

namespace wf {
namespace {
inline constexpr const char* kTotal[] = {"KNOWN","UNKNOWN"};
inline constexpr const char* kProv[] = {"DURABLE_KNOWN","RECONSTRUCTED","ESTIMATED","UNAVAILABLE"};
}  // namespace

std::string_view totalKindName(TotalKind k) noexcept { return static_cast<std::size_t>(k) < 2 ? kTotal[static_cast<std::size_t>(k)] : "INVALID"; }
std::string_view progressProvenanceName(ProgressProvenance p) noexcept { return static_cast<std::size_t>(p) < 4 ? kProv[static_cast<std::size_t>(p)] : "INVALID"; }

bool Progress::advance(std::uint64_t delta, bool durable) noexcept {
  // Integer overflow guard: never allow completion count to wrap.
  if (UINT64_MAX - completed < delta) return false;
  completed += delta;
  if (durable) { checkpointed = completed; provenance = ProgressProvenance::DURABLE_KNOWN; }
  lastAuthoritativeGeneration++;
  return true;
}

bool Progress::reconcile(std::uint64_t recovered, bool durable, ProgressProvenance prov) noexcept {
  if (monotonic && recovered < completed) return false;  // would regress
  completed = recovered;
  provenance = prov;
  if (durable) checkpointed = recovered;
  lastAuthoritativeGeneration++;
  return true;
}

}  // namespace wf
