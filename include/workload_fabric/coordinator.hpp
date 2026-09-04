#pragma once
// WorkloadEngine: the process-independent controller that applies generation-fenced
// lifecycle operations to durable Workload objects and persists each mutation.
// It owns no execution authority and no resource arbitration. A coordinator drives
// this engine against workers delivered over a transport; a test drives it directly.
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "workload_fabric/workload.hpp"
#include "workload_fabric/persistence.hpp"
#include "workload_fabric/outcome.hpp"
#include "workload_fabric/identity.hpp"

namespace wf {

// Encapsulated result of an engine operation carrying the workload generation the
// caller must present on the next authority-bearing call (fencing).
struct EngineResult {
  Outcome outcome = Outcome::UNKNOWN;
  std::optional<ExecutionEpisodeId> episodeId;
  WorkloadGeneration  workloadGeneration;
  std::optional<Explanation> explanation;
};

class WorkloadEngine {
 public:
  explicit WorkloadEngine(StateStore* store = nullptr);
  ~WorkloadEngine() = default;

  // ---- registration -----------------------------------------------------
  EngineResult create(const WorkloadId& id, const std::string& name, const std::string& intent, const Policy& policy);
  bool exists(const WorkloadId& id) const;

  // ---- lifecycle --------------------------------------------------------
  EngineResult validate(const WorkloadId& id);
  EngineResult setPolicy(const WorkloadId& id, const Policy& policy);
  EngineResult addDependency(const WorkloadId& id, const DependencyDef& def);
  EngineResult setDependencyStatus(const WorkloadId& id, std::size_t index, DependencyStatus status, DependencyGeneration observed);
  EngineResult addResourceRequirement(const WorkloadId& id, const ResourceRequirement& req);
  EngineResult applyGrant(const WorkloadId& id, const ResourceGrant& grant);
  EngineResult settle(const WorkloadId& id);

  // ---- execution --------------------------------------------------------
  EngineResult startEpisode(const WorkloadId& id, const OwnerId& owner, const OwnerGeneration& ownerGen);
  EngineResult admitDispatch(const WorkloadId& id, const ExecutionEpisodeId& episode, const WorkerId& worker, const WorkerBootId& boot);
  EngineResult progress(const WorkloadId& id, const ExecutionEpisodeId& episode, const ExecutionEpisodeGeneration& epGen, std::uint64_t delta, bool durable);
  EngineResult episodeCompleted(const WorkloadId& id, const ExecutionEpisodeId& episode);
  EngineResult commitCompletion(const WorkloadId& id, const WorkloadGeneration& expecting);
  EngineResult onWorkerLost(const WorkloadId& id, const WorkerId& worker, const WorkerBootId& boot, const WorkloadGeneration& expecting);
  EngineResult planRestart(const WorkloadId& id, const std::string& reason, const WorkloadGeneration& expecting);

  // ---- suspension / migration / cancellation ----------------------------
  EngineResult requestSuspend(const WorkloadId& id, const WorkloadGeneration& expecting);
  EngineResult completeSuspend(const WorkloadId& id, const WorkloadGeneration& expecting);
  EngineResult requestResume(const WorkloadId& id, const WorkloadGeneration& expecting);
  EngineResult requestMigration(const WorkloadId& id, const WorkloadGeneration& expecting);
  EngineResult commitMigration(const WorkloadId& id, const WorkloadGeneration& expecting, const WorkerId& targetWorker, const WorkerBootId& targetBoot);
  EngineResult cancel(const WorkloadId& id, const WorkloadGeneration& expecting);
  EngineResult completeCancellation(const WorkloadId& id, const WorkloadGeneration& expecting);

  // ---- inspection -------------------------------------------------------
  Workload snapshot(const WorkloadId& id) const;
  std::vector<WorkloadId> list() const;
  std::optional<Workload> find(const WorkloadId& id) const;

 private:
  Workload* get(const WorkloadId& id);
  Workload const* get(const WorkloadId& id) const;
  void persist(const Workload& wl);

  mutable std::mutex mtx_;
  std::map<WorkloadId, Workload> workloads_;
  StateStore* store_ = nullptr;
  std::uint64_t episodeCounter_ = 1000;
};

}  // namespace wf
