#pragma once
#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {

/**
 * Well-known resource monitor names.
 * NOTE: New resource monitors should use the well known name: envoy.resource_monitors.name.
 */
class ResourceMonitorNameValues {
public:
  // Heap monitor with statically configured max.
  const std::string FixedHeap = "envoy.resource_monitors.fixed_heap";

  // File-based injected resource monitor.
  const std::string InjectedResource = "envoy.resource_monitors.injected_resource";
};

using ResourceMonitorNames = ConstSingleton<ResourceMonitorNameValues>;

} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
