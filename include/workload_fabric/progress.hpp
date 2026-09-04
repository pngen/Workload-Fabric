#pragma once
// Workload-level progress. Progress is explicit and monotonic where the workload
// semantics allow it. We never fabricate percentages when total work is unknown.
#include <cstdint>
#include <optional>
#include <string_view>
#include "workload_fabric/identity.hpp"

namespace wf {

// Whether the total amount of work is known. UNKNOWN is not 'unknown so far'; it
// means the workload genuinely cannot state a total.
enum class TotalKind : std::uint8_t { KNOWN = 0, UNKNOWN = 1 };
std::string_view totalKindName(TotalKind k) noexcept;

// The provenance of a recovered progress value, so callers never mistake a guess
// for a durable fact.
enum class ProgressProvenance : std::uint8_t {
  DURABLE_KNOWN = 0,    // durably recorded by the workload advance authority
  RECONSTRUCTED = 1,    // derived from checkpoint/state reconstruction
  ESTIMATED = 2,        // extrapolated, non-authoritative
  UNAVAILABLE = 3,      // genuinely unavailable
};
std::string_view progressProvenanceName(ProgressProvenance p) noexcept;

struct Progress {
  std::uint64_t completed = 0;
  TotalKind     totalKind = TotalKind::UNKNOWN;
  std::uint64_t total = 0;
  // Volatile (in-memory, not yet durable) vs durable. Checkpointed progress is the
  // last value captured to durable checkpoint storage.
  std::uint64_t checkpointed = 0;
  ProgressProvenance provenance = ProgressProvenance::DURABLE_KNOWN;
  std::uint64_t lastAuthoritativeGeneration = 0;

  bool monotonic = true;  // whether the workload enforces monotonic progress

  // Completed is never allowed to decrease when monotonic. Returns false on regress.
  bool advance(std::uint64_t delta, bool durable) noexcept;
  // Absolutely set completed to a recovered value; fails if it would regress.
  bool reconcile(std::uint64_t recovered, bool durable, ProgressProvenance prov) noexcept;
  void regressToForTest(std::uint64_t v) noexcept { completed = v; }

  // Percentage has meaning only when total is KNOWN and non-zero.
  std::optional<double> percent() const noexcept {
    if (totalKind != TotalKind::KNOWN || total == 0) return std::nullopt;
    return static_cast<double>(completed) / static_cast<double>(total);
  }
  bool complete() const noexcept { return totalKind == TotalKind::KNOWN && completed >= total; }
};

}  // namespace wf
