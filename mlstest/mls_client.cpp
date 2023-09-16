#include <iostream>
#include <thread>

#include "namespace_config.h"
#include "pub_delegate.h"
#include "mls_client.h"
#include "sub_delegate.h"

using namespace mls;

MLSClient::MLSClient(const Config& config)
  : logger(config.logger)
{
  // Set up Quicr relay connection
  std::stringstream log_msg;
  log_msg.str("");
  log_msg << "Connecting to " << config.relay.hostname << ":" << config.relay.port;
  logger.log(qtransport::LogLevel::info, "");
  logger.log(qtransport::LogLevel::info, log_msg.str());

  qtransport::TransportConfig tcfg{ .tls_cert_filename = NULL,
                                    .tls_key_filename = NULL };
  // XXX(RLB): The first argument to this ctor should be const&.  Once that is
  // fixed, we can remove this copy.
  auto relay_copy = config.relay;
  client = std::make_unique<quicr::QuicRClient>(relay_copy, tcfg, logger);

  // Initialize MLS state
  const auto init_info = MLSInitInfo{ suite, config.user_id };
  if (config.is_creator) {
    const auto group_id = from_ascii("asdf"); // XXX
    mls_session = MLSSession::create(init_info, group_id );
  } else {
    mls_session = init_info;
  }
}

void
MLSClient::subscribe(quicr::Namespace ns)
{
  if (!sub_delegates.count(ns)) {
    sub_delegates[ns] = std::make_shared<SubDelegate>(this, logger);
  }

  std::stringstream log_msg;
  log_msg << "Subscribe to " << ns.to_hex();
  logger.log(qtransport::LogLevel::info, log_msg.str());

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
  logger.log(qtransport::LogLevel::info, "Now unsubscribing");
  client->unsubscribe(ns, {}, {});
}

void
MLSClient::join(quicr::Name& name)
{
  const auto ns = quicr::Namespace(name, 80);
  logger.log(qtransport::LogLevel::info,
             "Publish Intent for name: " + name.to_hex() +
               ", namespace: " + ns.to_hex());

  const auto pd = std::make_shared<PubDelegate>();
  client->publishIntent(pd, ns, {}, {}, {});
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // do publish
  logger.log(qtransport::LogLevel::info, "Publish, name=" + name.to_hex());
  const auto& kp = std::get<MLSInitInfo>(mls_session).key_package;
  auto kp_data = tls::marshal(kp);
  client->publishNamedObject(name, 0, 10000, false, std::move(kp_data));
}

void
MLSClient::publish(quicr::Namespace& ns, bytes&& data)
{

  const auto pd = std::make_shared<PubDelegate>();
  client->publishIntent(pd, ns, {}, {}, {});
  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::stringstream log_msg;
  log_msg << "Publish, name= " << ns.name().to_hex()
          << ", size=" << data.size();
  logger.log(qtransport::LogLevel::info, log_msg.str());
  client->publishNamedObject(ns.name(), 0, 10000, false, std::move(data));
}

void
MLSClient::handle(const quicr::Name& name, quicr::bytes&& data)
{
  const auto ns = quicr::Namespace(name, 80);
  const auto namespaces = NamespaceConfig::create_default();

  if (ns == namespaces.key_package) {
    if (!joined()) {
      logger.log(qtransport::LogLevel::info,
                 "Omit Key Package processing if not joined to the group");
      return;
    }
    logger.log(qtransport::LogLevel::info,
               "Received KeyPackage from participant.Add to MLS session ");
    auto& session = std::get<MLSSession>(mls_session);
    auto [welcome, commit] = session.add(std::move(data));

    logger.log(qtransport::LogLevel::info, "Publishing Welcome Message ");
    auto welcome_name = namespaces.welcome;
    publish(welcome_name, std::move(welcome));

    logger.log(qtransport::LogLevel::info, "Publishing Commit Message");
    auto commit_name = namespaces.commit;
    publish(commit_name, std::move(commit));
    return;
  }

  if (ns == namespaces.welcome) {
    logger.log(qtransport::LogLevel::info,
               "Received Welcome message from the creator. Processing it now ");

    if (joined()) {
      logger.log(qtransport::LogLevel::info,
                 "Omit Welcome processing if already joined to the group");
      return;
    }
    const auto& init_info = std::get<MLSInitInfo>(mls_session);
    mls_session = MLSSession::join(init_info, data);
    return;
  }

  if (ns == namespaces.commit) {
    logger.log(qtransport::LogLevel::info,
               "Commit message process is not implemented");
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
