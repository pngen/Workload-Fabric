# Workload Fabric — boundary specification

Workload Fabric is an open-source, vendor-neutral C++20 runtime for governing
long-running AI workload lifecycle across execution episodes, priorities,
dependencies, resource contracts, restart, migration, suspension, recovery, and
completion across heterogeneous accelerator infrastructure.

It answers one systems question:

**What is this workload supposed to accomplish, what lifecycle state is it in now,
what does it depend on, what resources and execution authority does it require,
and what must happen for it to continue, migrate, recover, or complete?**

## Where it sits

Workload Fabric sits above Execution Fabric and below higher-level schedulers,
workflow systems, and application orchestration.

## What Execution Fabric owns

- physical execution attempts
- execution ownership
- attempt generations
- dispatch authority
- stale-attempt fencing
- cancellation authority
- preemption/resume authority at the execution-attempt boundary
- completion authority
- logical commit

## What Workload Fabric owns

- the durable workload identity
- workload lifecycle
- workload intent
- execution episodes belonging to that workload
- workload-level progress
- priorities
- dependencies
- resource contracts
- lifecycle policy
- restart policy
- suspension
- migration
- recovery
- workload-level retries/restarts
- completion criteria
- failure terminality
- durable continuity across execution incarnations

## Non-goals

- It is **not** a scheduler. It exposes state and enforces workload semantics so a
  scheduler can make placement decisions against it.
- It does **not** own global resource arbitration. Resource Broker owns that.
- It does **not** own attempt-level execution authority. Execution Fabric owns that.
- It is **not** a generic DAG workflow engine. Dependency Fabric owns the deeper
  dependency-graph runtime; Workload Fabric has only the workload-facing model.
- It does **not** fabricate hardware capability. No multi-GPU, multi-node, MIG,
  RDMA, NVLink, transparent GPU-context migration, or hardware kernel preemption
  evidence is manufactured. Single-device migration is labeled as *single-device
  migration of workload execution authority and state between process/episode
  incarnations*. Synthetic scenarios are labeled **SYNTHETIC**.
