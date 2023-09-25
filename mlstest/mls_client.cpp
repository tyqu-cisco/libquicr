#include "mls_client.h"
#include "pub_delegate.h"
#include "sub_delegate.h"

#include <transport/transport.h>

#include <future>

using namespace mls;
using namespace std::chrono_literals;

static const uint16_t default_ttl_ms = 1000;

MLSClient::MLSClient(const Config& config)
  : logger(config.logger)
  , group_id(config.group_id)
  , user_id(config.user_id)
  , namespaces(NamespaceConfig(group_id))
  , mls_session(MLSInitInfo{ suite, user_id })
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
  if (as_creator) {
    const auto init_info = std::get<MLSInitInfo>(mls_session);
    mls_session = MLSSession::create(init_info, group_id);
  }

  // XXX(RLB) These subscriptions / publishes are done serially; we await a
  // response for each one before doing the next.  They could be done in
  // parallel by having subscribe/publish_intent return std::future<bool> and
  // awaiting all of these futures together.

  // Subscribe to the required namespaces
  auto success = true;
  success = success && subscribe(namespaces.key_package_sub());
  success = success && subscribe(namespaces.welcome_sub());
  success = success && subscribe(namespaces.commit_sub());
  success = success && subscribe(namespaces.leave_sub());

  // Announce intent to publish on this user's namespaces
  success = success && publish_intent(namespaces.key_package_pub(user_id));
  success = success && publish_intent(namespaces.welcome_pub(user_id));
  success = success && publish_intent(namespaces.commit_pub(user_id));
  success = success && publish_intent(namespaces.leave_pub(user_id));

  return success;
}

bool
MLSClient::join()
{
  const auto& kp = std::get<MLSInitInfo>(mls_session).key_package;
  const auto kp_id = NamespaceConfig::id_for(kp);
  const auto name = namespaces.for_key_package(user_id, kp_id);

  join_promise = std::promise<bool>();
  auto join_future = join_promise->get_future();

  publish(name, tls::marshal(kp));

  return join_future.get();
}

void
MLSClient::leave()
{
  auto self_remove = std::get<MLSSession>(mls_session).leave();
  const auto name = namespaces.for_leave(user_id);
  publish(name, std::move(self_remove));
}

bool
MLSClient::joined() const
{
  return std::holds_alternative<MLSSession>(mls_session);
}

const MLSSession&
MLSClient::session() const
{
  return std::get<MLSSession>(mls_session);
}

bool
operator==(const MLSClient::Epoch& lhs, const MLSClient::Epoch& rhs)
{
  return lhs.epoch == rhs.epoch &&
         lhs.member_count == rhs.member_count &&
         lhs.epoch_authenticator == rhs.epoch_authenticator;
}

MLSClient::Epoch
MLSClient::next_epoch()
{
  return epochs.pop();
}

bool
MLSClient::should_commit(size_t n_adds, const std::vector<mls::LeafIndex>& removed) const
{
  // TODO(richbarn): This method should be sensitive to what is being committed.
  // For example, the tree stays maximally full if the neighbor of a removed
  // node commits the remove.
  return session().should_commit(n_adds, removed);
}

bool
MLSClient::subscribe(quicr::Namespace ns)
{
  if (sub_delegates.count(ns)) {
    return true;
  }

  auto promise = std::promise<bool>();
  auto future = promise.get_future();
  const auto delegate =
    std::make_shared<SubDelegate>(*this, logger, std::move(promise));

  logger->Log("Subscribe to " + std::string(ns));
  quicr::bytes empty;
  client->subscribe(delegate,
                    ns,
                    quicr::SubscribeIntent::immediate,
                    "bogus_origin_url",
                    false,
                    "bogus_auth_token",
                    std::move(empty));

  const auto success = future.get();
  if (success) {
    sub_delegates.insert_or_assign(ns, delegate);
  }

  return success;
}

bool
MLSClient::publish_intent(quicr::Namespace ns)
{
  logger->Log("Publish Intent for namespace: " + std::string(ns));
  auto promise = std::promise<bool>();
  auto future = promise.get_future();
  const auto delegate =
    std::make_shared<PubDelegate>(logger, std::move(promise));

  client->publishIntent(delegate, ns, {}, {}, {});

  // XXX(RLB) `delegate` is destroyed at this point, because QuicRClient doesn't
  // hold strong references to its delegates.  As a result, though, QuicRClient
  // fails cleanly when its references are invalidated, and we don't need
  // anything further from the delegate.  So we can let it go.
  return future.get();
}

void
MLSClient::publish(const quicr::Name& name, bytes&& data)
{
  logger->Log("Publish, name=" + std::string(name) +
              " size=" + std::to_string(data.size()));
  client->publishNamedObject(name, 0, default_ttl_ms, false, std::move(data));
}

void
MLSClient::handle(const quicr::Name& name, quicr::bytes&& data)
{
  const auto [op, sender, third_name_value] = namespaces.parse(name);

  switch (op) {
    case NamespaceConfig::Operation::key_package: {
      logger->Log("Received KeyPackage");

      if (!joined()) {
        logger->Log("Ignoring KeyPackage; not joined to the group");
        return;
      }

      if (!should_commit(1, {})) {
        logger->Log("Ignoring KeyPackage; not the designated committer");
        return;
      }

      logger->Log("Adding new client to MLS session");
      auto& session = std::get<MLSSession>(mls_session);
      auto maybe_welcome_commit = session.add(sender, data);
      if (!maybe_welcome_commit) {
        logger->Log("Add failed");
        return;
      }

      auto [welcome, commit] = maybe_welcome_commit.value();

      logger->Log("Publishing Welcome Message ");
      const auto welcome_name =
        namespaces.for_welcome(user_id, third_name_value);
      publish(welcome_name, std::move(welcome));

      logger->Log("Publishing Commit Message");
      const auto epoch = session.get_state().epoch();
      const auto commit_name = namespaces.for_commit(user_id, epoch);
      publish(commit_name, std::move(commit));

      epochs.push({ session.get_state().epoch(),
                    session.member_count(),
                    session.get_state().epoch_authenticator() });

      logger->Log("Updated to epoch " +
                  std::to_string(session.get_state().epoch()));
      return;
    }

    case NamespaceConfig::Operation::welcome: {
      logger->Log("Received Welcome");

      if (joined()) {
        logger->Log("Ignoring Welcome; already joined to the group");
        return;
      }

      const auto& init_info = std::get<MLSInitInfo>(mls_session);
      const auto maybe_mls_session = MLSSession::join(init_info, data);
      if (!maybe_mls_session) {
        logger->Log("Ignoring Welcome; not for me");
        return;
      }

      mls_session = maybe_mls_session.value();
      if (join_promise) {
        join_promise->set_value(true);
      }

      const auto& session = std::get<MLSSession>(mls_session);
      epochs.push({ session.get_state().epoch(),
                    session.member_count(),
                    session.get_state().epoch_authenticator() });

      const auto welcome_ns = namespaces.welcome_sub();
      client->unsubscribe(welcome_ns, "bogus_origin_url", "bogus_auth_token");

      return;
    }

    case NamespaceConfig::Operation::leave: {
      logger->Log("Received Leave");

      if (!joined()) {
        logger->Log("Ignoring Leave; not joined to the group");
        return;
      }

      auto& session = std::get<MLSSession>(mls_session);
      auto maybe_removed = session.validate_leave(sender, data);
      if (!maybe_removed) {
        logger->Log("Invalid Leave request");
        return;
      }

      const auto removed = maybe_removed.value();
      if (!should_commit(0, { removed })) {
        logger->Log("Ignoring Leave; not the designated committer");
        return;
      }

      logger->Log("Removing client from MLS session");

      auto commit = session.remove(removed);

      logger->Log("Publishing Commit Message");
      const auto epoch = session.get_state().epoch();
      const auto commit_name = namespaces.for_commit(user_id, epoch);
      publish(commit_name, std::move(commit));

      epochs.push({ session.get_state().epoch(),
                    session.member_count(),
                    session.get_state().epoch_authenticator() });

      logger->Log("Updated to epoch " +
                  std::to_string(session.get_state().epoch()));
      return;
    }

    case NamespaceConfig::Operation::commit: {
      logger->Log("Received Commit");

      if (!joined()) {
        logger->Log("Ignoring Commit; not joined to the group");
        return;
      }

      auto& session = std::get<MLSSession>(mls_session);
      switch (session.handle(data)) {
        case MLSSession::HandleResult::ok: {
          epochs.push({ session.get_state().epoch(),
                        session.member_count(),
                        session.get_state().epoch_authenticator() });
          logger->Log("Updated to epoch " +
                      std::to_string(session.get_state().epoch()));
          break;
        }

        case MLSSession::HandleResult::stale: {
          logger->Log("Ignoring stale commit");
          break;
        }

        case MLSSession::HandleResult::future: {
          logger->Log("Ignoring commit for a future epoch");
          break;
        }
      }

      return;
    }

    default:
      throw std::runtime_error("Illegal operation in name: " +
                               std::to_string(op));
  }
}
