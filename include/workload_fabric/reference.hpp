#pragma once
// Reference in-process adapters for the narrow Execution Fabric, Resource Broker, and
// Checkpoint Provider interfaces. These make the standalone repository fully testable
// without linking the real (separate) fabric repositories, while preserving the exact
// interface contract Workload Fabric depends on.
#include "workload_fabric/execution.hpp"
#include "workload_fabric/resource_broker.hpp"
#include "workload_fabric/checkpoint.hpp"
#include <map>
#include <string>
#include <mutex>

namespace wf {

class ReferenceExecutionFabric : public ExecutionFabric {
 public:
  Outcome requestExecution(const ExecutionEpisodeId& episode, const std::string& intent) override;
  Outcome observeExecution(const ExecutionHandle& handle, ExecutionState& out) override;
  Outcome cancelExecution(const ExecutionHandle& handle) override;
  Outcome preemptExecution(const ExecutionHandle& handle) override;
  Outcome resumeExecution(const ExecutionHandle& handle) override;
  Outcome observeCompletion(const ExecutionHandle& handle, bool& out_authoritative) override;
  std::optional<ExecutionHandle> currentHandle(const ExecutionEpisodeId& episode) override;
  // test hook: report an authoritative completion for an episode.
  void reportCompletionForTest(const ExecutionEpisodeId& episode, std::uint64_t attemptGen);
 private:
  struct Entry { ExecutionHandle handle; ExecutionState state = ExecutionState::DISPATCHED; bool completed=false; };
  std::map<ExecutionEpisodeId, Entry> entries_;
  std::mutex mtx_;
};

class ReferenceBroker : public ResourceBroker {
 public:
  std::optional<ResourceGrant> requestGrant(const ResourceContract& contract) override;
  Outcome bindGrant(WorkloadId workload, ResourceGrant grant) override;
  Outcome releaseGrant(WorkloadId workload, ResourceGrant grant) override;
 private:
  std::map<WorkloadId, ResourceGrant> bound_;
  std::mutex mtx_;
};

class ReferenceCheckpoint : public CheckpointProvider {
 public:
  std::optional<CheckpointRef> writeCheckpoint(const ExecutionEpisodeId& episode, const std::string& token) override;
  std::optional<std::string> restoreCheckpoint(const ExecutionEpisodeId& target, CheckpointRef ref) override;
  bool valid(CheckpointRef ref, CheckpointGeneration gen) override;
 private:
  std::map<CheckpointRef, std::string> store_;
  CheckpointRef next_ = CheckpointRef::fromU64(1);
  std::mutex mtx_;
};

}  // namespace wf
