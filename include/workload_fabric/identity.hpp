#pragma once
// Workload Fabric identity domain.
//
// Every identity below is a distinct static C++ type so semantic authority
// domains can never be silently conflated. Identities are immutable and carry
// no mutable state. Generations are monotonic counters that fence stale
// authority: any mutation must present the generation it believes is current,
// and the runtime rejects authority carrying a stale generation.
//
//  0 is the reserved invalid value for every type here.
#include <cstdint>
#include "workload_fabric/detail/strong_id.hpp"

namespace wf {

// ---- Identity tags -------------------------------------------------------
struct WorkloadIdTag            {}; struct WorkloadGenerationTag        {}; struct WorkloadRevisionTag        {};
struct ExecutionEpisodeIdTag    {}; struct ExecutionEpisodeGenerationTag{};
struct CoordinatorEpochTag      {};
struct OwnerIdTag               {}; struct OwnerGenerationTag           {};
struct WorkerIdTag              {}; struct WorkerBootIdTag              {};
struct PriorityClassTag         {}; struct PriorityGenerationTag        {};
struct ResourceContractIdTag    {}; struct ResourceContractGenerationTag{};
struct DependencySetIdTag       {}; struct DependencyGenerationTag      {};
struct CheckpointRefTag         {}; struct CheckpointGenerationTag      {};
struct MigrationIdTag           {}; struct MigrationGenerationTag       {};
struct RestartGenerationTag     {}; struct RecoveryGenerationTag        {};
struct CompletionGenerationTag  {}; struct PolicyGenerationTag          {};

// ---- Typed aliases -------------------------------------------------------
using WorkloadId            = detail::UniqueValue<WorkloadIdTag>;
using WorkloadGeneration    = detail::Generation<WorkloadGenerationTag>;
using WorkloadRevision      = detail::Generation<WorkloadRevisionTag>;

using ExecutionEpisodeId    = detail::UniqueValue<ExecutionEpisodeIdTag>;
using ExecutionEpisodeGeneration = detail::Generation<ExecutionEpisodeGenerationTag>;

using CoordinatorEpoch      = detail::Generation<CoordinatorEpochTag>;

using OwnerId               = detail::UniqueValue<OwnerIdTag>;
using OwnerGeneration       = detail::Generation<OwnerGenerationTag>;

using WorkerId              = detail::UniqueValue<WorkerIdTag>;
using WorkerBootId          = detail::UniqueValue<WorkerBootIdTag>;

using PriorityClass         = detail::UniqueValue<PriorityClassTag>;
using PriorityGeneration    = detail::Generation<PriorityGenerationTag>;

using ResourceContractId    = detail::UniqueValue<ResourceContractIdTag>;
using ResourceContractGeneration = detail::Generation<ResourceContractGenerationTag>;

using DependencySetId       = detail::UniqueValue<DependencySetIdTag>;
using DependencyGeneration  = detail::Generation<DependencyGenerationTag>;

using CheckpointRef         = detail::UniqueValue<CheckpointRefTag>;
using CheckpointGeneration  = detail::Generation<CheckpointGenerationTag>;

using MigrationId           = detail::UniqueValue<MigrationIdTag>;
using MigrationGeneration   = detail::Generation<MigrationGenerationTag>;

using RestartGeneration     = detail::Generation<RestartGenerationTag>;
using RecoveryGeneration    = detail::Generation<RecoveryGenerationTag>;
using CompletionGeneration  = detail::Generation<CompletionGenerationTag>;
using PolicyGeneration      = detail::Generation<PolicyGenerationTag>;

// next() helper: produce the successor generation for any monotonic fence.
template <class Tag>
constexpr detail::Generation<Tag> next(detail::Generation<Tag> g) { return detail::Generation<Tag>::fromU64(g.toU64() + 1); }

}  // namespace wf
