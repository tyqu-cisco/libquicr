#include "mls_client.h"
#include "pub_delegate.h"
#include "sub_delegate.h"

#include <transport/transport.h>

#include <iostream>
#include <thread>

using namespace mls;

static const uint16_t default_ttl_ms = 1000;

MLSClient::MLSClient(const Config& config)
  : logger(config.logger)
  , group_id(config.group_id)
  , user_id(config.user_id)
  , namespaces(NamespaceConfig(group_id))
{
  // Set up Quicr relay connection
  logger->Log("Connecting to " + config.relay.hostname + ":" +
              std::to_string(config.relay.port));

  qtransport::TransportConfig tcfg{ .tls_cert_filename = NULL,
                                    .tls_key_filename = NULL };
  // XXX(RLB): The first argument to this ctor should be const&.  Once that is
  // fixed, we can remove this copy.
  auto relay_copy = config.relay;
  client = std::make_unique<quicr::QuicRClient>(relay_copy, tcfg, logger);
}

bool
MLSClient::connect(bool as_creator)
{
  // Connect to the quicr relay
  if (!client->connect()) {
    return false;
  }

  // Initialize MLS state
  const auto init_info = MLSInitInfo{ suite, std::to_string(user_id) };
  if (as_creator) {
    mls_session = MLSSession::create(init_info, tls::marshal(group_id));
  } else {
    mls_session = init_info;
  }

  // Subscribe to the required namespaces
  subscribe(namespaces.key_package_sub());
  subscribe(namespaces.welcome_sub());
  subscribe(namespaces.commit_sub());

  return true;
}

void
MLSClient::subscribe(quicr::Namespace ns)
{
  if (!sub_delegates.count(ns)) {
    sub_delegates[ns] = std::make_shared<SubDelegate>(*this, logger);
  }

  logger->Log("Subscribe to " + std::string(ns));

  quicr::bytes empty;
  client->subscribe(sub_delegates[ns],
                    ns,
                    quicr::SubscribeIntent::immediate,
                    "origin_url",
                    false,
                    "auth_token",
                    std::move(empty));
}

void
MLSClient::unsubscribe(quicr::Namespace ns)
{
  logger->Log("Now unsubscribing");
  client->unsubscribe(ns, {}, {});
}

void
MLSClient::join()
{
  const auto ns = namespaces.key_package_pub(user_id);

  const auto& kp = std::get<MLSInitInfo>(mls_session).key_package;
  const auto kp_id = NamespaceConfig::id_for(kp);
  const auto name = namespaces.for_key_package(user_id, kp_id);

  publish(ns, name, tls::marshal(kp));
}

void
MLSClient::publish(const quicr::Namespace& ns,
                   const quicr::Name& name,
                   bytes&& data)
{

  logger->Log("Publish Intent for namespace: " + std::string(ns));
  const auto pd = std::make_shared<PubDelegate>(logger);
  client->publishIntent(pd, ns, {}, {}, {});
  std::this_thread::sleep_for(std::chrono::seconds(1));

  logger->Log("Publish, name=" + std::string(name));
  client->publishNamedObject(name, 0, default_ttl_ms, false, std::move(data));
}

void
MLSClient::handle(const quicr::Name& name, quicr::bytes&& data)
{
  const auto [op, sender, third_name_value] = namespaces.parse(name);

  switch (op) {
    case NamespaceConfig::Operation::key_package: {
      if (!joined()) {
        logger->Log("Omit Key Package processing if not joined to the group");
        return;
      }
      logger->Log("Received KeyPackage from participant.Add to MLS session ");
      auto& session = std::get<MLSSession>(mls_session);
      auto [welcome, commit] = session.add(std::move(data));

      logger->Log("Publishing Welcome Message ");
      const auto welcome_ns = namespaces.welcome_pub(user_id);
      const auto welcome_name =
        namespaces.for_welcome(user_id, third_name_value);
      publish(welcome_ns, welcome_name, std::move(welcome));

      logger->Log("Publishing Commit Message");
      // TODO(RLB) Use epoch number from MLS session
      const auto epoch = uint64_t(0);
      const auto commit_ns = namespaces.commit_pub(user_id);
      const auto commit_name = namespaces.for_commit(user_id, epoch);
      publish(commit_ns, commit_name, std::move(commit));
      return;
    }

    case NamespaceConfig::Operation::welcome: {
      logger->Log(
        "Received Welcome message from the creator. Processing it now ");

      if (joined()) {
        logger->Log("Omit Welcome processing if already joined to the group");
        return;
      }
      const auto& init_info = std::get<MLSInitInfo>(mls_session);
      mls_session = MLSSession::join(init_info, data);
      return;
    }

    case NamespaceConfig::Operation::commit: {
      logger->Log("Commit message process is not implemented");
      return;
    }

    default:
      throw std::runtime_error("Illegal operation in name: " + std::to_string(op));
  }
}

const MLSSession&
MLSClient::session() const
{
  return std::get<MLSSession>(mls_session);
}

bool
MLSClient::joined() const
{
  return std::holds_alternative<MLSSession>(mls_session);
}
