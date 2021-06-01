#pragma once

#include "envoy/server/wasm_config.h"
#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class WasmFactoryContextImpl : public WasmFactoryContext {
public:
  WasmFactoryContextImpl(Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
                         Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls,
                         Api::Api& api, Stats::ScopeSharedPtr scope,
                         Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                         ServerLifecycleNotifier& lifecycle_notifier)
      : cluster_manager_(cluster_manager), init_manager_(init_manager), dispatcher_(dispatcher),
        tls_(tls), api_(api), scope_(scope), random_(random), local_info_(local_info),
        lifecycle_notifier_(lifecycle_notifier) {}

  Upstream::ClusterManager& clusterManager() override { return cluster_manager_; }
  Init::Manager& initManager() override { return init_manager_; }
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  ThreadLocal::SlotAllocator& threadLocal() override { return tls_; }
  Api::Api& api() override { return api_; }
  Stats::ScopeSharedPtr& scope() override { return scope_; }
  const LocalInfo::LocalInfo& localInfo() const override { return local_info_; }
  Envoy::Runtime::RandomGenerator& random() override { return random_; }
  ServerLifecycleNotifier& lifecycleNotifier() override { return lifecycle_notifier_; }

private:
  Upstream::ClusterManager& cluster_manager_;
  Init::Manager& init_manager_;
  Event::Dispatcher& dispatcher_;
  ThreadLocal::SlotAllocator& tls_;
  Api::Api& api_;
  Stats::ScopeSharedPtr scope_;
  Runtime::RandomGenerator& random_;
  const LocalInfo::LocalInfo& local_info_;
  ServerLifecycleNotifier& lifecycle_notifier_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
