#pragma once

#include <functional>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/init/manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_callbacks.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/callback_impl.h"
#include "common/common/cleanup.h"
#include "common/config/subscription_base.h"
#include "common/config/utility.h"
#include "common/init/target_impl.h"
#include "common/ssl/certificate_validation_context_config_impl.h"
#include "common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

/**
 * SDS API implementation that fetches secrets from SDS server via Subscription.
 */
class SdsApi : public Envoy::Config::SubscriptionBase<
                   envoy::extensions::transport_sockets::tls::v3::Secret> {
public:
  struct SecretData {
    const std::string resource_name_;
    std::string version_info_;
    SystemTime last_updated_;
  };

  SdsApi(envoy::config::core::v3::ConfigSource sds_config, absl::string_view sds_config_name,
         Config::SubscriptionFactory& subscription_factory, TimeSource& time_source,
         ProtobufMessage::ValidationVisitor& validation_visitor, Stats::Store& stats,
         Init::Manager& init_manager, std::function<void()> destructor_cb,
         Event::Dispatcher& dispatcher, Api::Api& api);

  SecretData secretData();

protected:
  // Creates new secrets.
  virtual void setSecret(const envoy::extensions::transport_sockets::tls::v3::Secret&) PURE;
  virtual void validateConfig(const envoy::extensions::transport_sockets::tls::v3::Secret&) PURE;
  Common::CallbackManager<> update_callback_manager_;

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>&,
                      const Protobuf::RepeatedPtrField<std::string>&, const std::string&) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::extensions::transport_sockets::tls::v3::Secret>(resource)
        .name();
  }
  virtual std::vector<std::string> getDataSourceFilenames() PURE;

private:
  void validateUpdateSize(int num_resources);
  void initialize();
  uint64_t getHashForFiles();

  Init::TargetImpl init_target_;
  Stats::Store& stats_;

  const envoy::config::core::v3::ConfigSource sds_config_;
  std::unique_ptr<Config::Subscription> subscription_;
  const std::string sds_config_name_;

  uint64_t secret_hash_;
  uint64_t files_hash_;
  Cleanup clean_up_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Config::SubscriptionFactory& subscription_factory_;
  TimeSource& time_source_;
  SecretData secret_data_;
  Event::Dispatcher& dispatcher_;
  Api::Api& api_;
  std::unique_ptr<Filesystem::Watcher> watcher_;
};

class TlsCertificateSdsApi;
class CertificateValidationContextSdsApi;
class TlsSessionTicketKeysSdsApi;
class GenericSecretSdsApi;
using TlsCertificateSdsApiSharedPtr = std::shared_ptr<TlsCertificateSdsApi>;
using CertificateValidationContextSdsApiSharedPtr =
    std::shared_ptr<CertificateValidationContextSdsApi>;
using TlsSessionTicketKeysSdsApiSharedPtr = std::shared_ptr<TlsSessionTicketKeysSdsApi>;
using GenericSecretSdsApiSharedPtr = std::shared_ptr<GenericSecretSdsApi>;

/**
 * TlsCertificateSdsApi implementation maintains and updates dynamic TLS certificate secrets.
 */
class TlsCertificateSdsApi : public SdsApi, public TlsCertificateConfigProvider {
public:
  static TlsCertificateSdsApiSharedPtr
  create(Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
         const envoy::config::core::v3::ConfigSource& sds_config,
         const std::string& sds_config_name, std::function<void()> destructor_cb) {
    // We need to do this early as we invoke the subscription factory during initialization, which
    // is too late to throw.
    Config::Utility::checkLocalInfo("TlsCertificateSdsApi", secret_provider_context.localInfo());
    return std::make_shared<TlsCertificateSdsApi>(
        sds_config, sds_config_name, secret_provider_context.clusterManager().subscriptionFactory(),
        secret_provider_context.dispatcher().timeSource(),
        secret_provider_context.messageValidationVisitor(), secret_provider_context.stats(),
        *secret_provider_context.initManager(), destructor_cb, secret_provider_context.dispatcher(),
        secret_provider_context.api());
  }

  TlsCertificateSdsApi(const envoy::config::core::v3::ConfigSource& sds_config,
                       const std::string& sds_config_name,
                       Config::SubscriptionFactory& subscription_factory, TimeSource& time_source,
                       ProtobufMessage::ValidationVisitor& validation_visitor, Stats::Store& stats,
                       Init::Manager& init_manager, std::function<void()> destructor_cb,
                       Event::Dispatcher& dispatcher, Api::Api& api)
      : SdsApi(sds_config, sds_config_name, subscription_factory, time_source, validation_visitor,
               stats, init_manager, std::move(destructor_cb), dispatcher, api) {}

  // SecretProvider
  const envoy::extensions::transport_sockets::tls::v3::TlsCertificate* secret() const override {
    return tls_certificate_secrets_.get();
  }
  Common::CallbackHandle* addValidationCallback(
      std::function<void(const envoy::extensions::transport_sockets::tls::v3::TlsCertificate&)>)
      override {
    return nullptr;
  }
  Common::CallbackHandle* addUpdateCallback(std::function<void()> callback) override {
    if (secret()) {
      callback();
    }
    return update_callback_manager_.add(callback);
  }

protected:
  void setSecret(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    tls_certificate_secrets_ =
        std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>(
            secret.tls_certificate());
  }
  void validateConfig(const envoy::extensions::transport_sockets::tls::v3::Secret&) override {}
  std::vector<std::string> getDataSourceFilenames() override;

private:
  TlsCertificatePtr tls_certificate_secrets_;
};

/**
 * CertificateValidationContextSdsApi implementation maintains and updates dynamic certificate
 * validation context secrets.
 */
class CertificateValidationContextSdsApi : public SdsApi,
                                           public CertificateValidationContextConfigProvider {
public:
  static CertificateValidationContextSdsApiSharedPtr
  create(Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
         const envoy::config::core::v3::ConfigSource& sds_config,
         const std::string& sds_config_name, std::function<void()> destructor_cb) {
    // We need to do this early as we invoke the subscription factory during initialization, which
    // is too late to throw.
    Config::Utility::checkLocalInfo("CertificateValidationContextSdsApi",
                                    secret_provider_context.localInfo());
    return std::make_shared<CertificateValidationContextSdsApi>(
        sds_config, sds_config_name, secret_provider_context.clusterManager().subscriptionFactory(),
        secret_provider_context.dispatcher().timeSource(),
        secret_provider_context.messageValidationVisitor(), secret_provider_context.stats(),
        *secret_provider_context.initManager(), destructor_cb, secret_provider_context.dispatcher(),
        secret_provider_context.api());
  }
  CertificateValidationContextSdsApi(const envoy::config::core::v3::ConfigSource& sds_config,
                                     const std::string& sds_config_name,
                                     Config::SubscriptionFactory& subscription_factory,
                                     TimeSource& time_source,
                                     ProtobufMessage::ValidationVisitor& validation_visitor,
                                     Stats::Store& stats, Init::Manager& init_manager,
                                     std::function<void()> destructor_cb,
                                     Event::Dispatcher& dispatcher, Api::Api& api)
      : SdsApi(sds_config, sds_config_name, subscription_factory, time_source, validation_visitor,
               stats, init_manager, std::move(destructor_cb), dispatcher, api) {}

  // SecretProvider
  const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
  secret() const override {
    return certificate_validation_context_secrets_.get();
  }
  Common::CallbackHandle* addUpdateCallback(std::function<void()> callback) override {
    if (secret()) {
      callback();
    }
    return update_callback_manager_.add(callback);
  }

  Common::CallbackHandle* addValidationCallback(
      std::function<
          void(const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&)>
          callback) override {
    return validation_callback_manager_.add(callback);
  }

protected:
  void setSecret(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    certificate_validation_context_secrets_ = std::make_unique<
        envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext>(
        secret.validation_context());
  }

  void
  validateConfig(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    validation_callback_manager_.runCallbacks(secret.validation_context());
  }
  std::vector<std::string> getDataSourceFilenames() override;

private:
  CertificateValidationContextPtr certificate_validation_context_secrets_;
  Common::CallbackManager<
      const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&>
      validation_callback_manager_;
};

/**
 * TlsSessionTicketKeysSdsApi implementation maintains and updates dynamic tls session ticket keys
 * secrets.
 */
class TlsSessionTicketKeysSdsApi : public SdsApi, public TlsSessionTicketKeysConfigProvider {
public:
  static TlsSessionTicketKeysSdsApiSharedPtr
  create(Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
         const envoy::config::core::v3::ConfigSource& sds_config,
         const std::string& sds_config_name, std::function<void()> destructor_cb) {
    // We need to do this early as we invoke the subscription factory during initialization, which
    // is too late to throw.
    Config::Utility::checkLocalInfo("TlsSessionTicketKeysSdsApi",
                                    secret_provider_context.localInfo());
    return std::make_shared<TlsSessionTicketKeysSdsApi>(
        sds_config, sds_config_name, secret_provider_context.clusterManager().subscriptionFactory(),
        secret_provider_context.dispatcher().timeSource(),
        secret_provider_context.messageValidationVisitor(), secret_provider_context.stats(),
        *secret_provider_context.initManager(), destructor_cb, secret_provider_context.dispatcher(),
        secret_provider_context.api());
  }

  TlsSessionTicketKeysSdsApi(const envoy::config::core::v3::ConfigSource& sds_config,
                             const std::string& sds_config_name,
                             Config::SubscriptionFactory& subscription_factory,
                             TimeSource& time_source,
                             ProtobufMessage::ValidationVisitor& validation_visitor,
                             Stats::Store& stats, Init::Manager& init_manager,
                             std::function<void()> destructor_cb, Event::Dispatcher& dispatcher,
                             Api::Api& api)
      : SdsApi(sds_config, sds_config_name, subscription_factory, time_source, validation_visitor,
               stats, init_manager, std::move(destructor_cb), dispatcher, api) {}

  // SecretProvider
  const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys*
  secret() const override {
    return tls_session_ticket_keys_.get();
  }

  Common::CallbackHandle* addUpdateCallback(std::function<void()> callback) override {
    if (secret()) {
      callback();
    }
    return update_callback_manager_.add(callback);
  }

  Common::CallbackHandle* addValidationCallback(
      std::function<
          void(const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys&)>
          callback) override {
    return validation_callback_manager_.add(callback);
  }

protected:
  void setSecret(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    tls_session_ticket_keys_ =
        std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys>(
            secret.session_ticket_keys());
  }

  void
  validateConfig(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    validation_callback_manager_.runCallbacks(secret.session_ticket_keys());
  }
  std::vector<std::string> getDataSourceFilenames() override;

private:
  Secret::TlsSessionTicketKeysPtr tls_session_ticket_keys_;
  Common::CallbackManager<
      const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys&>
      validation_callback_manager_;
};

/**
 * GenericSecretSdsApi implementation maintains and updates dynamic generic secret.
 */
class GenericSecretSdsApi : public SdsApi, public GenericSecretConfigProvider {
public:
  static GenericSecretSdsApiSharedPtr
  create(Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
         const envoy::config::core::v3::ConfigSource& sds_config,
         const std::string& sds_config_name, std::function<void()> destructor_cb) {
    // We need to do this early as we invoke the subscription factory during initialization, which
    // is too late to throw.
    Config::Utility::checkLocalInfo("GenericSecretSdsApi", secret_provider_context.localInfo());
    return std::make_shared<GenericSecretSdsApi>(
        sds_config, sds_config_name, secret_provider_context.clusterManager().subscriptionFactory(),
        secret_provider_context.dispatcher().timeSource(),
        secret_provider_context.messageValidationVisitor(), secret_provider_context.stats(),
        *secret_provider_context.initManager(), destructor_cb, secret_provider_context.dispatcher(),
        secret_provider_context.api());
  }

  GenericSecretSdsApi(const envoy::config::core::v3::ConfigSource& sds_config,
                      const std::string& sds_config_name,
                      Config::SubscriptionFactory& subscription_factory, TimeSource& time_source,
                      ProtobufMessage::ValidationVisitor& validation_visitor, Stats::Store& stats,
                      Init::Manager& init_manager, std::function<void()> destructor_cb,
                      Event::Dispatcher& dispatcher, Api::Api& api)
      : SdsApi(sds_config, sds_config_name, subscription_factory, time_source, validation_visitor,
               stats, init_manager, std::move(destructor_cb), dispatcher, api) {}

  // SecretProvider
  const envoy::extensions::transport_sockets::tls::v3::GenericSecret* secret() const override {
    return generic_secret.get();
  }
  Common::CallbackHandle* addUpdateCallback(std::function<void()> callback) override {
    return update_callback_manager_.add(callback);
  }
  Common::CallbackHandle* addValidationCallback(
      std::function<void(const envoy::extensions::transport_sockets::tls::v3::GenericSecret&)>
          callback) override {
    return validation_callback_manager_.add(callback);
  }

protected:
  void setSecret(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    generic_secret = std::make_unique<envoy::extensions::transport_sockets::tls::v3::GenericSecret>(
        secret.generic_secret());
  }
  void
  validateConfig(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) override {
    validation_callback_manager_.runCallbacks(secret.generic_secret());
  }
  std::vector<std::string> getDataSourceFilenames() override;

private:
  GenericSecretPtr generic_secret;
  Common::CallbackManager<const envoy::extensions::transport_sockets::tls::v3::GenericSecret&>
      validation_callback_manager_;
};

} // namespace Secret
} // namespace Envoy
