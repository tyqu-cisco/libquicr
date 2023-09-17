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
  , namespaces(NamespaceConfig::create_default())
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
MLSClient::connect(std::string user_id, bool as_creator)
{
  // Connect to the quicr relay
  if (!client->connect()) {
    return false;
  }

  // Initialize MLS state
  const auto init_info = MLSInitInfo{ suite, std::move(user_id) };
  if (as_creator) {
    const auto group_id = from_ascii("asdf"); // TODO(RLB): Set group ID
    mls_session = MLSSession::create(init_info, group_id);
  } else {
    mls_session = init_info;
  }

  // Subscribe to the required namespaces
  subscribe(namespaces.key_package);
  subscribe(namespaces.welcome);
  subscribe(namespaces.commit);

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
MLSClient::join(quicr::Name& name)
{
  const auto ns = quicr::Namespace(name, 80);
  logger->Log("Publish Intent for name: " + std::string(name) +
              ", namespace: " + std::string(ns));

  const auto pd = std::make_shared<PubDelegate>(logger);
  client->publishIntent(pd, ns, {}, {}, {});
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // do publish
  logger->Log("Publish, name=" + std::string(name));
  const auto& kp = std::get<MLSInitInfo>(mls_session).key_package;
  auto kp_data = tls::marshal(kp);
  client->publishNamedObject(
    name, 0, default_ttl_ms, false, std::move(kp_data));
}

void
MLSClient::publish(quicr::Namespace& ns, bytes&& data)
{
  const auto pd = std::make_shared<PubDelegate>(logger);
  client->publishIntent(pd, ns, {}, {}, {});
  std::this_thread::sleep_for(std::chrono::seconds(1));

  logger->Log("Publish, name= " + std::string(ns.name()) +
              ", size=" + std::to_string(data.size()));
  client->publishNamedObject(
    ns.name(), 0, default_ttl_ms, false, std::move(data));
}

void
MLSClient::handle(const quicr::Name& name, quicr::bytes&& data)
{
  if (namespaces.key_package.contains(name)) {
    if (!joined()) {
      logger->Log("Omit Key Package processing if not joined to the group");
      return;
    }
    logger->Log("Received KeyPackage from participant.Add to MLS session ");
    auto& session = std::get<MLSSession>(mls_session);
    auto [welcome, commit] = session.add(std::move(data));

    logger->Log("Publishing Welcome Message ");
    auto welcome_name = namespaces.welcome;
    publish(welcome_name, std::move(welcome));

    logger->Log("Publishing Commit Message");
    auto commit_name = namespaces.commit;
    publish(commit_name, std::move(commit));
    return;
  }

  if (namespaces.welcome.contains(name)) {
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

  if (namespaces.commit.contains(name)) {
    logger->Log("Commit message process is not implemented");
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
