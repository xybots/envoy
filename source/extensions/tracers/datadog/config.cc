#include "extensions/tracers/datadog/config.h"

#include "envoy/config/trace/v3/trace.pb.h"
#include "envoy/config/trace/v3/trace.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/datadog/datadog_tracer_impl.h"
#include "extensions/tracers/well_known_names.h"

#include "datadog/opentracing.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

DatadogTracerFactory::DatadogTracerFactory() : FactoryBase(TracerNames::get().Datadog) {}

Tracing::HttpTracerSharedPtr DatadogTracerFactory::createHttpTracerTyped(
    const envoy::config::trace::v3::DatadogConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  Tracing::DriverPtr datadog_driver = std::make_unique<Driver>(
      proto_config, context.serverFactoryContext().clusterManager(),
      context.serverFactoryContext().scope(), context.serverFactoryContext().threadLocal(),
      context.serverFactoryContext().runtime());
  return std::make_shared<Tracing::HttpTracerImpl>(std::move(datadog_driver),
                                                   context.serverFactoryContext().localInfo());
}

/**
 * Static registration for the Datadog tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(DatadogTracerFactory, Server::Configuration::TracerFactory);

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
