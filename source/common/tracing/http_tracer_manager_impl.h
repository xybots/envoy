#pragma once

#include "envoy/server/tracer_config.h"
#include "envoy/singleton/instance.h"
#include "envoy/tracing/http_tracer_manager.h"

#include "common/common/logger.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Tracing {

class HttpTracerManagerImpl : public HttpTracerManager,
                              public Singleton::Instance,
                              Logger::Loggable<Logger::Id::tracing> {
public:
  HttpTracerManagerImpl(Server::Configuration::TracerFactoryContextPtr factory_context);

  // HttpTracerManager
  HttpTracerSharedPtr
  getOrCreateHttpTracer(const envoy::config::trace::v3::Tracing_Http* config) override;

private:
  Server::Configuration::TracerFactoryContextPtr factory_context_;
  const HttpTracerSharedPtr null_tracer_{std::make_shared<Tracing::HttpNullTracer>()};

  // HttpTracers indexed by the hash of their configuration.
  absl::flat_hash_map<std::size_t, HttpTracerSharedPtr> http_tracers_;
};

} // namespace Tracing
} // namespace Envoy
