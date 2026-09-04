#include "workload_fabric/coordinator.hpp"

namespace wf {

WorkloadEngine::WorkloadEngine(StateStore* store) : store_(store) {}

bool WorkloadEngine::exists(const WorkloadId& id) const {
  std::lock_guard<std::mutex> g(mtx_);
  return workloads_.find(id) != workloads_.end();
}

Workload* WorkloadEngine::get(const WorkloadId& id) {
  auto it = workloads_.find(id);
  return it == workloads_.end() ? nullptr : &it->second;
}
Workload const* WorkloadEngine::get(const WorkloadId& id) const {
  auto it = workloads_.find(id);
  return it == workloads_.end() ? nullptr : &it->second;
}

void WorkloadEngine::persist(const Workload& wl) {
  if (store_) (void)store_->save(wl);
}

EngineResult WorkloadEngine::create(const WorkloadId& id, const std::string& name, const std::string& intent, const Policy& policy) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  if (workloads_.find(id) != workloads_.end()) { r.outcome = Outcome::REJECT_DUPLICATE_WORKLOAD; return r; }
  Workload w = Workload::create(id, name, intent);
  w.setPolicyForTest(policy);
  workloads_.emplace(id, std::move(w));
  r.outcome = Outcome::ALLOW;
  r.workloadGeneration = WorkloadGeneration::fromU64(1);
  if (store_) (void)store_->save(workloads_.at(id));
  return r;
}

EngineResult WorkloadEngine::validate(const WorkloadId& id) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.workloadGeneration = w->generation();
  r.outcome = w->transition(WorkloadState::VALIDATING, w->generation(), "validate");
  if (isAllow(r.outcome)) persist(*w);
  return r;
}

EngineResult WorkloadEngine::setPolicy(const WorkloadId& id, const Policy& policy) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  w->setPolicyForTest(policy);
  r.outcome = Outcome::ALLOW; r.workloadGeneration = w->generation();
  persist(*w);
  return r;
}

EngineResult WorkloadEngine::addDependency(const WorkloadId& id, const DependencyDef& def) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->dependencies().define(def);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}

EngineResult WorkloadEngine::setDependencyStatus(const WorkloadId& id, std::size_t index, DependencyStatus status, DependencyGeneration observed) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->dependencies().setStatus(index, status, observed);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}

EngineResult WorkloadEngine::addResourceRequirement(const WorkloadId& id, const ResourceRequirement& req) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->contract().add(req);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}

EngineResult WorkloadEngine::applyGrant(const WorkloadId& id, const ResourceGrant& grant) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->contract().applyGrant(grant);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}

EngineResult WorkloadEngine::settle(const WorkloadId& id) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->settle(w->generation());
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}

EngineResult WorkloadEngine::startEpisode(const WorkloadId& id, const OwnerId& owner, const OwnerGeneration& ownerGen) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  ExecutionEpisodeId ep(episodeCounter_++);
  r.outcome = w->startEpisode(ep, owner, ownerGen, true);
  if (isAllow(r.outcome)) { r.episodeId = ep; persist(*w); }
  r.workloadGeneration = w->generation();
  return r;
}

EngineResult WorkloadEngine::admitDispatch(const WorkloadId& id, const ExecutionEpisodeId& episode, const WorkerId& worker, const WorkerBootId& boot) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->admitDispatch(episode, worker, boot);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}

EngineResult WorkloadEngine::progress(const WorkloadId& id, const ExecutionEpisodeId& episode, const ExecutionEpisodeGeneration& epGen, std::uint64_t delta, bool durable) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->reportProgress(episode, epGen, delta, durable);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}

EngineResult WorkloadEngine::episodeCompleted(const WorkloadId& id, const ExecutionEpisodeId& episode) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->markEpisodeCompleted(episode);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}

EngineResult WorkloadEngine::commitCompletion(const WorkloadId& id, const WorkloadGeneration& expecting) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->commitCompletion(expecting);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}

EngineResult WorkloadEngine::onWorkerLost(const WorkloadId& id, const WorkerId& worker, const WorkerBootId& boot, const WorkloadGeneration& expecting) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->onWorkerLost(worker, boot, expecting);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}

EngineResult WorkloadEngine::planRestart(const WorkloadId& id, const std::string& reason, const WorkloadGeneration& expecting) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->planRestart(expecting, reason);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}

EngineResult WorkloadEngine::requestSuspend(const WorkloadId& id, const WorkloadGeneration& expecting) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->requestSuspend(expecting);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}
EngineResult WorkloadEngine::completeSuspend(const WorkloadId& id, const WorkloadGeneration& expecting) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->completeSuspend(expecting);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}
EngineResult WorkloadEngine::requestResume(const WorkloadId& id, const WorkloadGeneration& expecting) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->requestResume(expecting);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}
EngineResult WorkloadEngine::requestMigration(const WorkloadId& id, const WorkloadGeneration& expecting) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->requestMigration(expecting);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}
EngineResult WorkloadEngine::commitMigration(const WorkloadId& id, const WorkloadGeneration& expecting, const WorkerId& targetWorker, const WorkerBootId& targetBoot) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->commitMigration(expecting, targetWorker, targetBoot);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}
EngineResult WorkloadEngine::cancel(const WorkloadId& id, const WorkloadGeneration& expecting) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->requestCancellationWorkloadScope(expecting);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}
EngineResult WorkloadEngine::completeCancellation(const WorkloadId& id, const WorkloadGeneration& expecting) {
  std::lock_guard<std::mutex> g(mtx_);
  EngineResult r;
  Workload* w = get(id);
  if (!w) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; return r; }
  r.outcome = w->completeCancellation(expecting);
  r.workloadGeneration = w->generation();
  if (isAllow(r.outcome)) persist(*w);
  return r;
}

Workload WorkloadEngine::snapshot(const WorkloadId& id) const {
  std::lock_guard<std::mutex> g(mtx_);
  auto* w = const_cast<WorkloadEngine*>(this)->get(id);
  if (!w) return {};
  return *w;
}
std::vector<WorkloadId> WorkloadEngine::list() const {
  std::lock_guard<std::mutex> g(mtx_);
  std::vector<WorkloadId> out;
  for (auto& p : workloads_) out.push_back(p.first);
  return out;
}
std::optional<Workload> WorkloadEngine::find(const WorkloadId& id) const {
  std::lock_guard<std::mutex> g(mtx_);
  auto* w = const_cast<WorkloadEngine*>(this)->get(id);
  if (!w) return std::nullopt;
  return *w;
}

}  // namespace wf
