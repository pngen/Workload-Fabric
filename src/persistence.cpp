#include "workload_fabric/persistence.hpp"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <system_error>

namespace wf {

std::uint32_t under(WorkloadState s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(EpisodeState s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(ResourceKind s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(NeedLevel s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(GrantStatus s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(DependencyRequirement s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(DependencyReady s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(DependencyStatus s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(PriorityTier s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(TotalKind s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(ProgressProvenance s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(RestartKind s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(CompletionKind s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(MigrationEligibility s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(CancelRaceSemantics s) { return static_cast<std::uint32_t>(s); }
std::uint32_t under(Outcome s) { return static_cast<std::uint32_t>(s); }

void Workload::encode(detail::Writer& w) const {
  w.u64(id_.toU64());
  w.u64(generation_.toU64());
  w.u64(revision_.toU64());
  w.u8(static_cast<std::uint8_t>(state_));
  w.str(name_);
  w.str(intent_);

  w.u8(static_cast<std::uint8_t>(policy_.restart.kind));
  w.u32(policy_.restart.maxRestarts);
  w.u32(policy_.restart.maxRecoverySeconds);
  w.u8(policy_.restart.carryProgressForward ? 1 : 0);
  w.u8(policy_.restart.carryCheckpoint ? 1 : 0);
  w.u8(static_cast<std::uint8_t>(policy_.completion.kind));
  w.u32(policy_.completion.requiredUnits);
  w.u8(policy_.completion.requireDurableArtifact ? 1 : 0);
  w.u8(policy_.completion.allowZeroWorkCompletion ? 1 : 0);
  w.u8(static_cast<std::uint8_t>(policy_.migration.eligibility));
  w.u8(policy_.migration.requireCheckpoint ? 1 : 0);
  w.u8(policy_.migration.requireQuiescence ? 1 : 0);
  w.u8(policy_.migration.allowLossy ? 1 : 0);
  w.u8(policy_.suspension.allowed ? 1 : 0);
  w.u8(policy_.suspension.requireDurableState ? 1 : 0);
  w.u32(policy_.suspension.maxSuspendedSeconds);
  w.u8(policy_.suspension.releaseResourcesWhileSuspended ? 1 : 0);
  w.u8(static_cast<std::uint8_t>(policy_.cancelRace));
  w.u64(policy_.generation.toU64());

  w.u64(contract_.id().toU64());
  w.u64(contract_.generation().toU64());
  w.u32(static_cast<std::uint32_t>(contract_.requirements().size()));
  for (const auto& r : contract_.requirements()) {
    w.u8(static_cast<std::uint8_t>(r.kind));
    w.u8(static_cast<std::uint8_t>(r.level));
    w.f64(r.minValue);
    w.f64(r.maxValue);
    w.str(r.spec);
  }
  w.u64(contract_.grant().contractId.toU64());
  w.u64(contract_.grant().contractGeneration.toU64());
  w.u8(static_cast<std::uint8_t>(contract_.grant().status));
  w.u8(contract_.grant().released ? 1 : 0);
  w.u32(static_cast<std::uint32_t>(contract_.grant().bindings.size()));
  for (const auto& b : contract_.grant().bindings) { w.u8(static_cast<std::uint8_t>(b.kind)); w.f64(b.amount); w.str(b.qualifier); }

  w.u64(deps_.id().toU64());
  w.u64(deps_.generation().toU64());
  w.u32(static_cast<std::uint32_t>(deps_.defs().size()));
  for (const auto& d : deps_.defs()) { w.u64(d.dependsOn.toU64()); w.u8(static_cast<std::uint8_t>(d.requirement)); w.u8(static_cast<std::uint8_t>(d.readyWhen)); w.str(d.artifactKey); }
  w.u32(static_cast<std::uint32_t>(deps_.states().size()));
  for (const auto& s : deps_.states()) { w.u8(static_cast<std::uint8_t>(s.status)); w.u64(s.observedGeneration.toU64()); w.str(s.detail); }

  w.u64(priority_.cls().toU64());
  w.u64(priority_.generation().toU64());
  w.u8(static_cast<std::uint8_t>(priority_.baseTier()));
  w.i64(priority_.baseValue());
  w.i64(priority_.promotion());
  w.u8(priority_.starved() ? 1 : 0);
  w.u8(priority_.preemptible() ? 1 : 0);
  w.u8(priority_.protectedFromPreemption() ? 1 : 0);

  w.u64(progress_.completed);
  w.u8(static_cast<std::uint8_t>(progress_.totalKind));
  w.u64(progress_.total);
  w.u64(progress_.checkpointed);
  w.u8(static_cast<std::uint8_t>(progress_.provenance));
  w.u64(progress_.lastAuthoritativeGeneration);
  w.u8(progress_.monotonic ? 1 : 0);

  w.u64(owner_.toU64());
  w.u64(ownerGeneration_.toU64());
  w.u64(epoch_.toU64());
  w.u32(restartCount_);
  w.u64(restartGen_.toU64());
  w.u64(migrationId_.toU64());
  w.u64(migrationGen_.toU64());
  w.u64(completionGen_.toU64());
  w.u8(cancellationRequested_ ? 1 : 0);
  w.u8(cancelledFinal_ ? 1 : 0);
  w.u8(cancellationWasRejected_ ? 1 : 0);
  w.str(lastRestartReason_);
  w.u64(lastSourceEpisode_.toU64());

  w.u32(static_cast<std::uint32_t>(episodes_.size()));
  for (const auto& ep : episodes_) {
    w.u64(ep.id.toU64()); w.u64(ep.generation.toU64()); w.u8(static_cast<std::uint8_t>(ep.state));
    w.u64(ep.owner.toU64()); w.u64(ep.ownerGeneration.toU64());
    w.u64(ep.worker.toU64()); w.u64(ep.boot.toU64());
    w.u64(ep.handle.worker.toU64()); w.u64(ep.handle.boot.toU64()); w.u64(ep.handle.attemptGeneration); w.str(ep.handle.intent);
    w.u64(ep.restartGeneration.toU64()); w.u64(ep.recoveryGeneration.toU64()); w.u64(ep.checkpoint.toU64()); w.u64(ep.checkpointGeneration.toU64());
    w.u64(ep.migrationId.toU64()); w.u64(ep.migrationGeneration.toU64());
    w.u64(ep.progress.completed); w.u8(static_cast<std::uint8_t>(ep.progress.totalKind)); w.u64(ep.progress.total); w.u64(ep.progress.checkpointed);
    w.u8(static_cast<std::uint8_t>(ep.progress.provenance)); w.u64(ep.progress.lastAuthoritativeGeneration); w.u8(ep.progress.monotonic ? 1 : 0);
    w.str(ep.intent);
    w.u8(ep.authoritative ? 1 : 0); w.u8(ep.failed ? 1 : 0); w.u8(ep.cancelled ? 1 : 0); w.u8(ep.completed ? 1 : 0); w.u8(ep.workerLost ? 1 : 0);
  }
  w.u32(static_cast<std::uint32_t>(history_.size()));
  for (const auto& h : history_) {
    w.u8(static_cast<std::uint8_t>(h.from)); w.u8(static_cast<std::uint8_t>(h.to)); w.u64(h.generation.toU64()); w.u64(h.timestampMs);
    w.str(h.reason); w.u8(static_cast<std::uint8_t>(h.outcome));
  }
}

// Read an enum with validity range check. Returns false and sets status on invalid.
template <class F>
static bool readEnum(detail::Reader& r, std::uint8_t maxV, F&& set, Outcome& status) {
  auto v = r.u8();
  if (v > maxV) { status = Outcome::REJECT_UNKNOWN; return false; }
  set(v);
  return true;
}

Workload Workload::decode(detail::Reader& r, Outcome& status) {
  Workload w;
  status = Outcome::ALLOW;
  w.id_ = WorkloadId::fromU64(r.u64());
  w.generation_ = WorkloadGeneration::fromU64(r.u64());
  w.revision_ = WorkloadRevision::fromU64(r.u64());
  if (!readEnum(r, 25, [&](std::uint8_t v){ w.state_ = static_cast<WorkloadState>(v); }, status)) return {};
  w.name_ = r.str();
  w.intent_ = r.str();

  if (!readEnum(r, 6, [&](std::uint8_t v){ w.policy_.restart.kind = static_cast<RestartKind>(v); }, status)) return {};
  w.policy_.restart.maxRestarts = r.u32();
  w.policy_.restart.maxRecoverySeconds = r.u32();
  w.policy_.restart.carryProgressForward = r.u8() != 0;
  w.policy_.restart.carryCheckpoint = r.u8() != 0;
  if (!readEnum(r, 3, [&](std::uint8_t v){ w.policy_.completion.kind = static_cast<CompletionKind>(v); }, status)) return {};
  w.policy_.completion.requiredUnits = r.u32();
  w.policy_.completion.requireDurableArtifact = r.u8() != 0;
  w.policy_.completion.allowZeroWorkCompletion = r.u8() != 0;
  if (!readEnum(r, 2, [&](std::uint8_t v){ w.policy_.migration.eligibility = static_cast<MigrationEligibility>(v); }, status)) return {};
  w.policy_.migration.requireCheckpoint = r.u8() != 0;
  w.policy_.migration.requireQuiescence = r.u8() != 0;
  w.policy_.migration.allowLossy = r.u8() != 0;
  w.policy_.suspension.allowed = r.u8() != 0;
  w.policy_.suspension.requireDurableState = r.u8() != 0;
  w.policy_.suspension.maxSuspendedSeconds = r.u32();
  w.policy_.suspension.releaseResourcesWhileSuspended = r.u8() != 0;
  if (!readEnum(r, 1, [&](std::uint8_t v){ w.policy_.cancelRace = static_cast<CancelRaceSemantics>(v); }, status)) return {};
  w.policy_.generation = PolicyGeneration::fromU64(r.u64());

  w.contract_ = ResourceContract(ResourceContractId::fromU64(r.u64()), ResourceContractGeneration::fromU64(r.u64()));
  std::uint32_t nr = r.u32();
  std::vector<ResourceRequirement> reqs;
  reqs.reserve(nr);
  for (std::uint32_t i = 0; i < nr; ++i) {
    ResourceRequirement req;
    if (!readEnum(r, 16, [&](std::uint8_t v){ req.kind = static_cast<ResourceKind>(v); }, status)) return {};
    if (!readEnum(r, 3, [&](std::uint8_t v){ req.level = static_cast<NeedLevel>(v); }, status)) return {};
    req.minValue = r.f64(); req.maxValue = r.f64();
    if (!std::isfinite(req.minValue) || !std::isfinite(req.maxValue) || req.minValue < 0) { status = Outcome::REJECT_UNKNOWN; return {}; }
    req.spec = r.str();
    reqs.push_back(std::move(req));
  }
  w.contract_.setRequirementsNoAdvance(reqs);
  ResourceGrant g;
  g.contractId = ResourceContractId::fromU64(r.u64());
  g.contractGeneration = ResourceContractGeneration::fromU64(r.u64());
  if (!readEnum(r, 5, [&](std::uint8_t v){ g.status = static_cast<GrantStatus>(v); }, status)) return {};
  g.released = r.u8() != 0;
  std::uint32_t nb = r.u32();
  for (std::uint32_t i = 0; i < nb; ++i) { ResourceBinding b; if (!readEnum(r, 16, [&](std::uint8_t v){ b.kind = static_cast<ResourceKind>(v); }, status)) return {}; b.amount = r.f64(); if (!std::isfinite(b.amount) || b.amount < 0) { status = Outcome::REJECT_UNKNOWN; return {}; } b.qualifier = r.str(); g.bindings.push_back(std::move(b)); }
  w.contract_.setGrantForTest(std::move(g));

  w.deps_ = DependencySet(DependencySetId::fromU64(r.u64()), DependencyGeneration::fromU64(r.u64()));
  std::uint32_t nd = r.u32();
  for (std::uint32_t i = 0; i < nd; ++i) { DependencyDef d; d.dependsOn = WorkloadId::fromU64(r.u64()); if (!readEnum(r, 1, [&](std::uint8_t v){ d.requirement = static_cast<DependencyRequirement>(v); }, status)) return {}; if (!readEnum(r, 2, [&](std::uint8_t v){ d.readyWhen = static_cast<DependencyReady>(v); }, status)) return {}; d.artifactKey = r.str(); w.deps_.addDefForTest(d); }
  std::uint32_t ns = r.u32();
  for (std::uint32_t i = 0; i < ns; ++i) { DependencyState s; if (!readEnum(r, 4, [&](std::uint8_t v){ s.status = static_cast<DependencyStatus>(v); }, status)) return {}; s.observedGeneration = DependencyGeneration::fromU64(r.u64()); s.detail = r.str(); w.deps_.setStateForTest(i, std::move(s)); }

  w.priority_.setBase(PriorityClass::fromU64(r.u64()), PriorityTier::NORMAL, 0);
  w.priority_.setPriorityGen(PriorityGeneration::fromU64(r.u64()));
  if (!readEnum(r, 5, [&](std::uint8_t v){ w.priority_.setBaseTier(static_cast<PriorityTier>(v)); }, status)) return {};
  auto baseV = r.i64(); auto promo = r.i64();
  w.priority_.setPriorityInts(static_cast<int>(baseV), static_cast<int>(promo));
  auto starved = r.u8() != 0; auto preemptible = r.u8() != 0; auto protectedFlag = r.u8() != 0;
  w.priority_.setFlagsForTest(starved, preemptible, protectedFlag);

  w.progress_.completed = r.u64();
  if (!readEnum(r, 1, [&](std::uint8_t v){ w.progress_.totalKind = static_cast<TotalKind>(v); }, status)) return {};
  w.progress_.total = r.u64();
  w.progress_.checkpointed = r.u64();
  if (!readEnum(r, 3, [&](std::uint8_t v){ w.progress_.provenance = static_cast<ProgressProvenance>(v); }, status)) return {};
  w.progress_.lastAuthoritativeGeneration = r.u64();
  w.progress_.monotonic = r.u8() != 0;

  w.owner_ = OwnerId::fromU64(r.u64());
  w.ownerGeneration_ = OwnerGeneration::fromU64(r.u64());
  w.epoch_ = CoordinatorEpoch::fromU64(r.u64());
  w.restartCount_ = r.u32();
  w.restartGen_ = RestartGeneration::fromU64(r.u64());
  w.migrationId_ = MigrationId::fromU64(r.u64());
  w.migrationGen_ = MigrationGeneration::fromU64(r.u64());
  w.completionGen_ = CompletionGeneration::fromU64(r.u64());
  w.cancellationRequested_ = r.u8() != 0;
  w.cancelledFinal_ = r.u8() != 0;
  w.cancellationWasRejected_ = r.u8() != 0;
  w.lastRestartReason_ = r.str();
  w.lastSourceEpisode_ = ExecutionEpisodeId::fromU64(r.u64());

  std::uint32_t ne = r.u32();
  for (std::uint32_t i = 0; i < ne; ++i) {
    Episode ep;
    ep.id = ExecutionEpisodeId::fromU64(r.u64()); ep.generation = ExecutionEpisodeGeneration::fromU64(r.u64());
    if (!readEnum(r, 9, [&](std::uint8_t v){ ep.state = static_cast<EpisodeState>(v); }, status)) return {};
    ep.owner = OwnerId::fromU64(r.u64()); ep.ownerGeneration = OwnerGeneration::fromU64(r.u64());
    ep.worker = WorkerId::fromU64(r.u64()); ep.boot = WorkerBootId::fromU64(r.u64());
    ep.handle.worker = WorkerId::fromU64(r.u64()); ep.handle.boot = WorkerBootId::fromU64(r.u64()); ep.handle.attemptGeneration = r.u64(); ep.handle.intent = r.str();
    ep.restartGeneration = RestartGeneration::fromU64(r.u64()); ep.recoveryGeneration = RecoveryGeneration::fromU64(r.u64());
    ep.checkpoint = CheckpointRef::fromU64(r.u64()); ep.checkpointGeneration = CheckpointGeneration::fromU64(r.u64());
    ep.migrationId = MigrationId::fromU64(r.u64()); ep.migrationGeneration = MigrationGeneration::fromU64(r.u64());
    ep.progress.completed = r.u64(); if (!readEnum(r, 1, [&](std::uint8_t v){ ep.progress.totalKind = static_cast<TotalKind>(v); }, status)) return {};
    ep.progress.total = r.u64(); ep.progress.checkpointed = r.u64();
    if (!readEnum(r, 3, [&](std::uint8_t v){ ep.progress.provenance = static_cast<ProgressProvenance>(v); }, status)) return {};
    ep.progress.lastAuthoritativeGeneration = r.u64(); ep.progress.monotonic = r.u8() != 0;
    ep.intent = r.str();
    ep.authoritative = r.u8() != 0; ep.failed = r.u8() != 0; ep.cancelled = r.u8() != 0; ep.completed = r.u8() != 0; ep.workerLost = r.u8() != 0;
    w.episodes_.push_back(std::move(ep));
  }
  std::uint32_t nh = r.u32();
  for (std::uint32_t i = 0; i < nh; ++i) {
    TransitionRecord h;
    if (!readEnum(r, 25, [&](std::uint8_t v){ h.from = static_cast<WorkloadState>(v); }, status)) return {};
    if (!readEnum(r, 25, [&](std::uint8_t v){ h.to = static_cast<WorkloadState>(v); }, status)) return {};
    h.generation = WorkloadGeneration::fromU64(r.u64()); h.timestampMs = r.u64(); h.reason = r.str();
    if (!readEnum(r, 36, [&](std::uint8_t v){ h.outcome = static_cast<Outcome>(v); }, status)) return {};
    w.history_.push_back(std::move(h));
  }

  // Reject trailing garbage.
  if (!r.empty()) { status = Outcome::REJECT_UNKNOWN; return {}; }
  // Reject impossible: invalid identity or zero generation.
  if (!w.id_.valid() || w.generation_.toU64() == 0 || w.revision_.toU64() == 0) { status = Outcome::REJECT_UNKNOWN; return {}; }
  return w;
}

std::vector<std::uint8_t> encodeWorkload(const Workload& wl) {
  detail::Writer pw;
  wl.encode(pw);
  auto payload = pw.take();
  detail::Writer fw;
  fw.u32(kPersistenceMagic);
  fw.u32(kPersistenceFormatVersion);
  fw.u64(payload.size());
  fw.raw(payload.data(), payload.size());
  fw.u64(detail::fnv1a64(payload.data(), payload.size()));
  return fw.take();
}

Workload decodeWorkload(const std::vector<std::uint8_t>& data, Outcome& status) {
  status = Outcome::UNKNOWN;
  try {
    detail::Reader fr(data);
    if (fr.u32() != kPersistenceMagic) { status = Outcome::REJECT_UNKNOWN; return {}; }
    if (fr.u32() != kPersistenceFormatVersion) { status = Outcome::REJECT_UNKNOWN; return {}; }
    std::uint64_t payloadLen = fr.u64();
    if (payloadLen > fr.remaining()) { status = Outcome::REJECT_UNKNOWN; return {}; }
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(payloadLen));
    fr.raw(payload.data(), static_cast<std::size_t>(payloadLen));
    std::uint64_t stored = fr.u64();
    if (stored != detail::fnv1a64(payload.data(), payload.size())) { status = Outcome::REJECT_UNKNOWN; return {}; }
    detail::Reader pr(payload);
    return Workload::decode(pr, status);
  } catch (detail::CodecError const&) {
    status = Outcome::REJECT_UNKNOWN;
    return {};
  }
}

// ---- FileStateStore -------------------------------------------------------
FileStateStore::FileStateStore(std::string directory) : dir_(std::move(directory)) {}

std::string FileStateStore::filePath(WorkloadId id) const {
  return dir_ + "/wf-" + std::to_string(id.toU64()) + ".bin";
}

Outcome FileStateStore::save(const Workload& wl) {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  if (ec) return Outcome::REJECT_UNKNOWN;
  auto bytes = encodeWorkload(wl);
  std::ofstream ofs(filePath(wl.id()), std::ios::binary | std::ios::trunc);
  if (!ofs) return Outcome::REJECT_UNKNOWN;
  ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!ofs) return Outcome::REJECT_UNKNOWN;
  return Outcome::ALLOW;
}

LoadResult FileStateStore::load(WorkloadId id) {
  LoadResult r;
  std::ifstream ifs(filePath(id), std::ios::binary | std::ios::ate);
  if (!ifs) { r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; r.error = "file not found"; return r; }
  auto size = ifs.tellg();
  if (size < 0) { r.outcome = Outcome::REJECT_UNKNOWN; r.error = "bad stream"; return r; }
  std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
  ifs.seekg(0, std::ios::beg);
  if (!ifs.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) { r.outcome = Outcome::REJECT_UNKNOWN; r.error = "read failed"; return r; }
  Outcome st = Outcome::UNKNOWN;
  Workload wl = decodeWorkload(data, st);
  if (!isAllow(st)) { r.outcome = st; r.error = "corrupt/truncated record"; return r; }
  r.workload = std::move(wl); r.outcome = Outcome::ALLOW;
  return r;
}

std::vector<WorkloadId> FileStateStore::listIds() const {
  std::vector<WorkloadId> out;
  std::error_code ec;
  for (auto& ent : std::filesystem::directory_iterator(dir_, ec)) {
    auto fn = ent.path().filename().string();
    if (fn.rfind("wf-", 0) == 0 && fn.size() > 5) {
      try { auto id = std::stoull(fn.substr(3, fn.size() - 7)); out.push_back(WorkloadId::fromU64(id)); } catch (...) {}
    }
  }
  return out;
}

bool FileStateStore::remove(WorkloadId id) {
  std::error_code ec;
  return std::filesystem::remove(filePath(id), ec);
}

// ---- MemoryStateStore -----------------------------------------------------
Outcome MemoryStateStore::save(const Workload& wl) {
  for (auto& p : records_) if (p.first == wl.id()) { p.second = encodeWorkload(wl); return Outcome::ALLOW; }
  records_.emplace_back(wl.id(), encodeWorkload(wl));
  return Outcome::ALLOW;
}
LoadResult MemoryStateStore::load(WorkloadId id) {
  LoadResult r;
  for (auto& p : records_) {
    if (p.first == id) {
      Outcome st = Outcome::UNKNOWN;
      Workload wl = decodeWorkload(p.second, st);
      if (!isAllow(st)) { r.outcome = st; r.error = "corrupt record"; return r; }
      r.workload = std::move(wl); r.outcome = Outcome::ALLOW; return r;
    }
  }
  r.outcome = Outcome::REJECT_WORKLOAD_NOT_FOUND; r.error = "no such workload";
  return r;
}
std::vector<WorkloadId> MemoryStateStore::listIds() const {
  std::vector<WorkloadId> out;
  for (auto& p : records_) out.push_back(p.first);
  return out;
}
bool MemoryStateStore::remove(WorkloadId id) {
  for (std::size_t i = 0; i < records_.size(); ++i) if (records_[i].first == id) { records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(i)); return true; }
  return false;
}

}  // namespace wf
