#include "extensions/tracers/opencensus/config.h"

#include "envoy/config/trace/v3/trace.pb.h"
#include "envoy/config/trace/v3/trace.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/opencensus/opencensus_tracer_impl.h"
#include "extensions/tracers/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {

OpenCensusTracerFactory::OpenCensusTracerFactory() : FactoryBase(TracerNames::get().OpenCensus) {}

Tracing::HttpTracerSharedPtr OpenCensusTracerFactory::createHttpTracerTyped(
    const envoy::config::trace::v3::OpenCensusConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  // Since OpenCensus can only support a single tracing configuration per entire process,
  // we need to make sure that it is configured at most once.
  if (tracer_) {
    if (Envoy::Protobuf::util::MessageDifferencer::Equals(config_, proto_config)) {
      return tracer_;
    } else {
      throw EnvoyException("Opencensus has already been configured with a different config.");
    }
  }
  Tracing::DriverPtr driver =
      std::make_unique<Driver>(proto_config, context.serverFactoryContext().localInfo(),
                               context.serverFactoryContext().api());
  tracer_ = std::make_shared<Tracing::HttpTracerImpl>(std::move(driver),
                                                      context.serverFactoryContext().localInfo());
  config_ = proto_config;
  return tracer_;
}

/**
 * Static registration for the OpenCensus tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(OpenCensusTracerFactory, Server::Configuration::TracerFactory);

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
