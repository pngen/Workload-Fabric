#pragma once
// Resource Broker integration. Workload Fabric owns the resource *contract*; the
// Broker owns cross-resource arbitration. This is the narrow interface Workload
// Fabric uses to request/bind/release grants. Tests use a reference adapter.
#include <string>
#include <optional>
#include "workload_fabric/outcome.hpp"
#include "workload_fabric/resource.hpp"
#include "workload_fabric/identity.hpp"

namespace wf {
class ResourceBroker {
 public:
  virtual ~ResourceBroker() = default;

  // Request a grant for the given contract. Returns a grant or an outcome.
  virtual std::optional<ResourceGrant> requestGrant(const ResourceContract& contract) = 0;

  // Bind an existing grant to a workload (making it live for this episode).
  virtual Outcome bindGrant(WorkloadId workload, ResourceGrant grant) = 0;

  // Release a grant, returning whether it was actually held (guards double release).
  virtual Outcome releaseGrant(WorkloadId workload, ResourceGrant grant) = 0;
};
}  // namespace wf
