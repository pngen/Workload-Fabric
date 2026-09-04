# Workload Fabric

**Open-source, vendor-neutral C++20 runtime for governing long-running AI workload lifecycle across execution episodes, priorities, dependencies, resource contracts, restart, migration, suspension, recovery, and completion across heterogeneous accelerator infrastructure.**

It answers one systems question:

> **What is this workload supposed to accomplish, what lifecycle state is it in now, what does it depend on, what resources and execution authority does it require, and what must happen for it to continue, migrate, recover, or complete?**

Workload Fabric sits **above** Execution Fabric and **below** higher-level schedulers, workflow systems, and application orchestration.

## The defining thesis

> **A workload is not a process. It is a durable unit of computational intent whose identity, progress, dependencies, resource contract, lifecycle, and completion must remain coherent while physical execution moves, restarts, suspends, fails, and recovers underneath it.**

The central abstraction is:

> **one durable logical workload → zero or more execution episodes → zero or more physical execution attempts**

A long-running workload may wait for admission, wait for dependencies, become runnable, execute, pause, be preempted, suspend voluntarily, checkpoint, lose its worker, restart, migrate to another node or accelerator, resume from durable state, change resource allocations, change priority, enter degraded execution, fail recoverably, fail terminally, complete, be cancelled, or be superseded. **The workload identity survives all of those transitions.**

## Boundaries

Workload Fabric is deliberately narrow. It does **not** re-implement adjacent layers; it integrates with them through narrow interfaces and enforces workload semantics against them.

| Layer | Owns | Workload Fabric relationship |
|------|------|------------------------------|
| **Execution Fabric** | physical execution attempts, execution ownership, attempt generations, dispatch/cancellation/preemption authority, completion authority, logical commit | Workload Fabric drives it through `ExecutionFabric` and never duplicates attempt-level authority |
| **Resource Broker** | cross-resource arbitration of scarce infrastructure | Workload Fabric owns the resource *contract* the workload requires/has been granted, and its lifecycle relationship |
| **Checkpoint Fabric** | durable execution-state capture and restoration | Workload Fabric references checkpoints through `CheckpointProvider` |
| **Failure Fabric** | failure semantics and recovery classification | (future) — Workload Fabric models failure *terminality* |
| **Dependency Fabric** | deeper dependency-graph execution and cascading recovery | Workload Fabric owns only the workload-facing dependency gating model |
| **Workload Fabric** | durable workload identity, lifecycle, intent, episodes, progress, priorities, dependencies, resource contracts, policy, restart, migration, suspension, recovery, completion criteria, failure terminality, durable continuity | the layer itself |

Workload Fabric is **not** a scheduler, not a workflow engine, not a Resource Broker clone, and not an Execution Fabric clone.

## What it provides

- **Durable workload identity** (`WorkloadId`, `WorkloadGeneration`, `WorkloadRevision`) plus strongly-typed domain identities for episodes, owners, workers, `WorkerBootId`, priority, resource contracts, dependency sets, checkpoints, migrations, and generations.
- **Explicit, guarded lifecycle state machine** (`WorkloadState`, `EpisodeState`) with a constexpr transition table. No shortcuts; illegal transitions are rejected with a deterministic `Outcome`.
- **Execution-episode model** — `Workload → Episode → Execution Fabric attempts`, with explicit authority and generation fencing.
- **Resource contracts** — REQUIRED / PREFERRED / OPTIONAL / PROHIBITED dimensions, requested/granted/bound/released/stale/expired grants, generation-fenced contract evolution.
- **Dependency gating** — REQUIRED vs OPTIONAL dependencies, readiness/completion/artifact requirements, stale-generation rejection, deterministic `BLOCKED_DEPENDENCY` explanations.
- **Priority semantics** — base + effective, promotion/demotion, starvation protection, preemption eligibility.
- **Progress semantics** — explicit, monotonic, durable vs volatile vs checkpointed, UNKNOWN total never fabricated into a percentage.
- **Restart policy** — NEVER / ON_KNOWN_FAILURE / ON_WORKER_LOSS / ON_INFRASTRUCTURE_FAILURE / ON_AMBIGUOUS_EXECUTION / ALWAYS_WITH_LIMIT / MANUAL_ONLY, with provable restart-limit enforcement (`max_restarts=N` never restarts `N+1` times).
- **Suspension / resume**, **migration**, **workload-scope cancellation**, and **workload-level completion criteria** (`SINGLE_RESULT`, `ALL_PHASES`, `REQUIRED_UNITS`, `EXPLICIT_FINALIZE`) with deterministic COMPLETE-vs-CANCEL race semantics.
- **Versioned binary persistence** with strong integrity checks that reject truncation, corruption, malformed lengths, invalid enums, impossible combinations, and trailing garbage.
- **Real multiprocess coordinator + workers** over framed loopback TCP with HELLO/REGISTER, fresh `WorkerBootId`, bounded decoding, real worker kill/restart, stale-authority rejection, and durable reconstruction.
- **Deterministic explanations** — `Outcome` taxonomy (ALLOW, DEFER, BLOCKED_DEPENDENCY, BLOCKED_RESOURCE_CONTRACT, REJECT_*, REVALIDATION_REQUIRED, UNKNOWN) so a caller can always ask *why*.
- **CUDA-backed proofs** on a local RTX 5090 (`sm_120`) — normal execution, restart under a fresh `WorkerBootId`, single-device process/episode migration, suspension/resume, and deterministic cancellation, each with exact CPU-parity and baseline device-memory return.
- **Property-based tests, concurrency race tests, benchmarks, a CLI inspection tool, examples, and an installable CMake package** with a downstream `find_package` consumer.

## Build

Requires CMake ≥ 3.24 and a C++20 compiler.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

For the CUDA proofs (single-GPU `sm_120`), build with the **Ninja** generator (the Visual Studio CUDA toolset is not reliably present):

```bash
tools\wf_env.cmd cmake -S . -B build-cuda -G Ninja -DWorkloadFabric_BUILD_CUDA=ON -DCMAKE_BUILD_TYPE=Release
tools\wf_env.cmd cmake --build build-cuda --target wf_cuda_proof
build-cuda\cuda\wf_cuda_proof.exe
```

## Install & consume

```bash
cmake --install build --prefix <prefix>
```

Then a downstream project uses `find_package(WorkloadFabric CONFIG REQUIRED)` and links the exported `WorkloadFabric::WorkloadFabric` target. See `examples/consumer/`.

## Tests

| Suite | Coverage |
|-------|----------|
| `wf_core` | lifecycle transition guards, zero-work gate, restart-limit off-by-one, stale authority, persistence round-trip, single-byte corruption sweep |
| `wf_scenarios` | dependency gating, resource-contract fencing, priority, cancellation/completion race, migration, suspension, restart-always-policy, reference Execution Fabric |
| `wf_property` | randomized legal/illegal event streams with invariant checks (single generation, no generation rollback, no terminal reopen, restart limit, monotonic progress, persistence round-trip) |
| `wf_concurrency` | real thread races: progress-vs-restart, cancel-vs-complete, cross-workload isolation, contract-mutation-vs-dispatch |
| `wf_mp_proof` | real multiprocess coordinator + two worker processes, real worker kill/restart, fresh boot, stale rejection, resume, completion, reconstruction |
| `wf_cuda_proof` | CUDA-backed scenarios A–E on `sm_120` |

## Honest hardware claims

The CUDA proofs are single-device on a local RTX 5090. They do **not** manufacture multi-GPU, multi-node, MIG, RDMA, NVLink, transparent GPU-context migration, or hardware kernel-preemption evidence. Migration is labeled accurately as **single-device migration of workload execution authority and state between process/episode incarnations**. Synthetic topology/resource scenarios are only ever explicitly labeled **SYNTHETIC**.

## License

MIT — see `LICENSE`.
