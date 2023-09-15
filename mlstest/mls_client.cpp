#include <iostream>
#include <thread>

#include "namespace_config.h"
#include "pub_delegate.h"
#include "mls_client.h"
#include "sub_delegate.h"

using namespace mls;

MLSClient::MLSClient(const Config& config)
  : is_user_creator(config.is_creator)
  , user(config.user_id)
  , group("1234")
  , logger(config.logger)
  , session(setupMLSSession(config.user_id, group, config.is_creator))
{
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
  client = new quicr::QuicRClient{ relay_copy, tcfg, logger };
}

void
MLSClient::subscribe(quicr::Namespace nspace)
{
  if (!client) {
    return;
  }

  if (!sub_delegates.count(nspace)) {
    sub_delegates[nspace] = std::make_shared<SubDelegate>(this, logger);
  }

  logger.log(qtransport::LogLevel::info, "Subscribe");

  std::stringstream log_msg;

  log_msg.str(std::string());
  log_msg.clear();

  log_msg << "Subscribe to " << nspace.to_hex();
  logger.log(qtransport::LogLevel::info, log_msg.str());

  quicr::SubscribeIntent intent = quicr::SubscribeIntent::immediate;
  quicr::bytes empty;
  client->subscribe(sub_delegates[nspace],
                    nspace,
                    intent,
                    "origin_url",
                    false,
                    "auth_token",
                    std::move(empty));
}

void
MLSClient::unsubscribe(quicr::Namespace nspace)
{
  logger.log(qtransport::LogLevel::info, "Now unsubscribing");
  client->unsubscribe(nspace, {}, {});
}

void
MLSClient::publishJoin(quicr::Name& name)
{
  auto nspace = quicr::Namespace(name, 80);
  logger.log(qtransport::LogLevel::info,
             "Publish Intent for name: " + name.to_hex() +
               ", namespace: " + nspace.to_hex());
  auto pd = std::make_shared<PubDelegate>();
  client->publishIntent(pd, nspace, {}, {}, {});
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // do publish
  logger.log(qtransport::LogLevel::info, "Publish, name=" + name.to_hex());
  auto kp_data = tls::marshal(user_info_map.at(user).key_package);
  client->publishNamedObject(name, 0, 10000, false, std::move(kp_data));
}

void
MLSClient::publishData(quicr::Namespace& nspace, bytes&& data)
{

  auto pd = std::make_shared<PubDelegate>();
  client->publishIntent(pd, nspace, {}, {}, {});
  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::stringstream log_msg;
  log_msg << "Publish, name= " << nspace.name().to_hex()
          << ", size=" << data.size();
  logger.log(qtransport::LogLevel::info, log_msg.str());
  client->publishNamedObject(nspace.name(), 0, 10000, false, std::move(data));
}

void
MLSClient::handle(const quicr::Name& name, quicr::bytes&& data)
{
  const auto ns = quicr::Namespace(name, 80);
  const auto namespaces = NamespaceConfig::create_default();

  if (ns == namespaces.key_package) {
    if (!is_user_creator) {
      logger.log(qtransport::LogLevel::info,
                 "Omit Key Package processing if not the creator");
      return;
    }
    logger.log(qtransport::LogLevel::info,
               "Received KeyPackage from participant.Add to MLS session ");
    auto [welcome, commit] = session->add(std::move(data));

    logger.log(qtransport::LogLevel::info, "Publishing Welcome Message ");
    auto welcome_name = namespaces.welcome;
    publishData(welcome_name, std::move(welcome));

    logger.log(qtransport::LogLevel::info, "Publishing Commit Message");
    auto commit_name = namespaces.commit;
    publishData(commit_name, std::move(commit));
    return;
  }

  if (ns == namespaces.welcome) {

    logger.log(qtransport::LogLevel::info,
               "Received Welcome message from the creator. Processing it now ");

    if (is_user_creator) {
      // do nothing
      return;
    }
    session = MLSSession::join(user_info_map.at(user), std::move(data));
    return;
  }

  if (ns == namespaces.commit) {
    logger.log(qtransport::LogLevel::info,
               "Commit message process is not implemented");
  }
}

bool
MLSClient::isUserCreator()
{
  return is_user_creator;
};

MLSSession&
MLSClient::getSession() const
{
  if (session == nullptr) {
    throw std::runtime_error("MLS Session is null");
  }
  return *session.get();
}

// private

std::unique_ptr<MLSSession>
MLSClient::setupMLSSession(const std::string& user,
                                   const std::string& group,
                                   bool is_creator)
{

  user_info_map.emplace(user, MLSInitInfo{ suite, user, group });

  if (is_creator) {
    return MLSSession::create(user_info_map[user]);
  }
  // the session will be created as part of welcome processing
  return nullptr;
}
