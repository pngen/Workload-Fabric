#include "workload_fabric/reference.hpp"

namespace wf {

// ---- ReferenceExecutionFabric ---------------------------------------------
Outcome ReferenceExecutionFabric::requestExecution(const ExecutionEpisodeId& episode, const std::string& intent) {
  std::lock_guard<std::mutex> g(mtx_);
  Entry e;
  e.handle.attemptGeneration = 1;
  e.handle.intent = intent;
  e.state = ExecutionState::DISPATCHED;
  entries_[episode] = e;
  return Outcome::ALLOW;
}

Outcome ReferenceExecutionFabric::observeExecution(const ExecutionHandle& handle, ExecutionState& out) {
  std::lock_guard<std::mutex> g(mtx_);
  for (auto& [ep, entry] : entries_) {
    if (entry.handle.worker == handle.worker && entry.handle.boot == handle.boot) { out = entry.state; return Outcome::ALLOW; }
  }
  return Outcome::REJECT_EPISODE_INELIGIBLE;
}

Outcome ReferenceExecutionFabric::cancelExecution(const ExecutionHandle& handle) {
  std::lock_guard<std::mutex> g(mtx_);
  for (auto& [ep, entry] : entries_) if (entry.handle.boot == handle.boot) entry.state = ExecutionState::CANCELLED;
  return Outcome::ALLOW;
}

Outcome ReferenceExecutionFabric::preemptExecution(const ExecutionHandle& handle) {
  std::lock_guard<std::mutex> g(mtx_);
  for (auto& [ep, entry] : entries_) if (entry.handle.boot == handle.boot) entry.state = ExecutionState::PREEMPTED;
  return Outcome::ALLOW;
}

Outcome ReferenceExecutionFabric::resumeExecution(const ExecutionHandle& handle) {
  std::lock_guard<std::mutex> g(mtx_);
  for (auto& [ep, entry] : entries_) if (entry.handle.boot == handle.boot) entry.state = ExecutionState::RUNNING;
  return Outcome::ALLOW;
}

Outcome ReferenceExecutionFabric::observeCompletion(const ExecutionHandle& handle, bool& out_authoritative) {
  std::lock_guard<std::mutex> g(mtx_);
  for (auto& [ep, entry] : entries_) {
    if (entry.handle.boot == handle.boot) { out_authoritative = entry.completed; return Outcome::ALLOW; }
  }
  return Outcome::REJECT_EPISODE_INELIGIBLE;
}

std::optional<ExecutionHandle> ReferenceExecutionFabric::currentHandle(const ExecutionEpisodeId& episode) {
  std::lock_guard<std::mutex> g(mtx_);
  auto it = entries_.find(episode);
  if (it == entries_.end()) return std::nullopt;
  return it->second.handle;
}

void ReferenceExecutionFabric::reportCompletionForTest(const ExecutionEpisodeId& episode, std::uint64_t attemptGen) {
  std::lock_guard<std::mutex> g(mtx_);
  auto& e = entries_[episode];
  e.completed = true;
  e.state = ExecutionState::COMPLETED;
  e.handle.attemptGeneration = attemptGen;
}

// ---- ReferenceBroker -------------------------------------------------------
std::optional<ResourceGrant> ReferenceBroker::requestGrant(const ResourceContract& contract) {
  ResourceGrant g;
  g.contractId = contract.id();
  g.contractGeneration = contract.generation();
  g.status = GrantStatus::GRANTED;
  g.released = false;
  // Mirror every required numeric dimension as a bound binding.
  for (const auto& r : contract.requirements()) {
    if (r.level == NeedLevel::REQUIRED && r.minValue > 0) g.bindings.push_back({r.kind, r.minValue, "ref"});
  }
  return g;
}
Outcome ReferenceBroker::bindGrant(WorkloadId workload, ResourceGrant grant) {
  std::lock_guard<std::mutex> g(mtx_);
  bound_[workload] = std::move(grant);
  return Outcome::ALLOW;
}
Outcome ReferenceBroker::releaseGrant(WorkloadId workload, ResourceGrant grant) {
  (void)grant;
  std::lock_guard<std::mutex> g(mtx_);
  auto it = bound_.find(workload);
  if (it == bound_.end()) return Outcome::REJECT_RESOURCE_DOUBLE_RELEASE;
  if (it->second.released) return Outcome::REJECT_RESOURCE_DOUBLE_RELEASE;
  it->second.released = true;
  return Outcome::ALLOW;
}

// ---- ReferenceCheckpoint ---------------------------------------------------
std::optional<CheckpointRef> ReferenceCheckpoint::writeCheckpoint(const ExecutionEpisodeId& episode, const std::string& token) {
  (void)episode;
  std::lock_guard<std::mutex> g(mtx_);
  CheckpointRef ref = CheckpointRef::fromU64(next_.toU64() + 1);
  next_ = ref;
  store_[ref] = token;
  return ref;
}
std::optional<std::string> ReferenceCheckpoint::restoreCheckpoint(const ExecutionEpisodeId& target, CheckpointRef ref) {
  (void)target;
  std::lock_guard<std::mutex> g(mtx_);
  auto it = store_.find(ref);
  if (it == store_.end()) return std::nullopt;
  return it->second;
}
bool ReferenceCheckpoint::valid(CheckpointRef ref, CheckpointGeneration gen) {
  (void)gen;
  std::lock_guard<std::mutex> g(mtx_);
  return store_.find(ref) != store_.end();
}

}  // namespace wf
