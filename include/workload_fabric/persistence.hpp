#pragma once
// Versioned binary persistence. Durable identity, lifecycle, generations,
// resource-contract state, dependency state, priority, episodes, restart /
// migration / recovery history, checkpoint refs, durable progress, completion
// state, and policy versions are all persisted together as one integrity-checked
// record per workload.
//
// The store is deliberately narrow: it knows how to persist a Workload record.
// It does NOT persist "live process authority" as unquestioned current truth.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <utility>
#include "workload_fabric/workload.hpp"
#include "workload_fabric/outcome.hpp"

namespace wf {

inline constexpr std::uint32_t kPersistenceFormatVersion = 1;
inline constexpr std::uint32_t kPersistenceMagic = 0x57464252u;  // 'W','F','B','R'

struct LoadResult {
  std::optional<Workload> workload;
  Outcome outcome = Outcome::UNKNOWN;
  std::string error;
};

// Narrow persistence interface. Implementations must reject truncation,
// corruption, malformed lengths, invalid enums, impossible combinations, and
// trailing garbage.
class StateStore {
 public:
  virtual ~StateStore() = default;
  virtual Outcome save(const Workload& wl) = 0;
  virtual LoadResult load(WorkloadId id) = 0;
  virtual std::vector<WorkloadId> listIds() const = 0;
  virtual bool remove(WorkloadId id) = 0;
};

// ---- codec ----------------------------------------------------------------
std::vector<std::uint8_t> encodeWorkload(const Workload& wl);
Workload decodeWorkload(const std::vector<std::uint8_t>& data, Outcome& status);

// ---- stores ---------------------------------------------------------------
class FileStateStore : public StateStore {
 public:
  explicit FileStateStore(std::string directory);
  ~FileStateStore() override = default;
  Outcome save(const Workload& wl) override;
  LoadResult load(WorkloadId id) override;
  std::vector<WorkloadId> listIds() const override;
  bool remove(WorkloadId id) override;

 private:
  std::string filePath(WorkloadId id) const;
  std::string dir_;
};

class MemoryStateStore : public StateStore {
 public:
  Outcome save(const Workload& wl) override;
  LoadResult load(WorkloadId id) override;
  std::vector<WorkloadId> listIds() const override;
  bool remove(WorkloadId id) override;

 private:
  std::vector<std::pair<WorkloadId, std::vector<std::uint8_t>>> records_;
};

}  // namespace wf
