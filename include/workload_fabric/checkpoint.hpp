#pragma once
// Checkpoint integration. Checkpoint Fabric owns durable execution-state capture;
// Workload Fabric only references checkpoints and asks Checkpoint Fabric to
// materialize/restore them. This is deliberately narrow.
#include <optional>
#include <string>
#include "workload_fabric/identity.hpp"
#include "workload_fabric/outcome.hpp"

namespace wf {
class CheckpointProvider {
 public:
  virtual ~CheckpointProvider() = default;

  // Write a checkpoint; returns a durable reference on success.
  virtual std::optional<CheckpointRef> writeCheckpoint(const ExecutionEpisodeId& episode, const std::string& token) = 0;

  // Restore a checkpoint for a target episode; returns a token, or nullopt.
  virtual std::optional<std::string> restoreCheckpoint(const ExecutionEpisodeId& target, CheckpointRef ref) = 0;

  // Whether a checkpoint reference is still valid (not expired/truncated).
  virtual bool valid(CheckpointRef ref, CheckpointGeneration gen) = 0;
};
}  // namespace wf
