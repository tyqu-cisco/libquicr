#include "mls_client.h"
#include "pub_delegate.h"
#include "sub_delegate.h"

#include <transport/transport.h>

using namespace mls;
using namespace std::chrono_literals;

static const uint16_t default_ttl_ms = 1000;

MLSClient::MLSClient(const Config& config)
  : logger(config.logger)
  , group_id(config.group_id)
  , user_id(config.user_id)
  , namespaces(NamespaceConfig(group_id))
  , mls_session(MLSInitInfo{ suite, user_id })
  , inbound_objects(std::make_shared<AsyncQueue<QuicrObject>>())
{
  // Set up Quicr relay connection
  logger->Log("Connecting to " + config.relay.hostname + ":" +
              std::to_string(config.relay.port));

  qtransport::TransportConfig tcfg{ .tls_cert_filename = NULL,
                                    .tls_key_filename = NULL };
  client = std::make_unique<quicr::Client>(config.relay, tcfg, logger);
}

MLSClient::~MLSClient()
{
  disconnect();
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
  success = success && subscribe(namespaces.commit_vote_sub());

  // Announce intent to publish on this user's namespaces
  success = success && publish_intent(namespaces.key_package_pub(user_id));
  success = success && publish_intent(namespaces.welcome_pub(user_id));
  success = success && publish_intent(namespaces.commit_pub(user_id));
  success = success && publish_intent(namespaces.leave_pub(user_id));
  success = success && publish_intent(namespaces.commit_vote_pub(user_id));

  if (!success) {
    return false;
  }

  // Start up a thread to handle incoming messages
  // TODO(richbarn): Have appropriate mutexes on internal state
  handler_thread = std::thread([&]() {
    while (!handler_thread_stop) {
      auto maybe_obj = inbound_objects->pop(inbound_object_timeout);
      if (!maybe_obj) {
        continue;
      }

      auto& obj = maybe_obj.value();
      handle(obj.name, std::move(obj.data));
    }

    logger->Log("Handler thread stopping");
  });

  return true;
}

void
MLSClient::disconnect()
{
  logger->Log("Disconnecting QuicR client");
  client->disconnect();

  logger->Log("Stopping handler thread");
  if (handler_thread && handler_thread.value().joinable()) {
    handler_thread_stop = true;
    handler_thread.value().join();
    logger->Log("Handler thread stopped");
  }
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

  // XXX(richbarn) It is important to disconnect here, before the Commit shows
  // up removing this client.  If we receive that Commit, we will crash with
  // "Invalid proposal list" becase we are trying to handle a Commit that
  // removes us.
  disconnect();
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
  return lhs.epoch == rhs.epoch && lhs.member_count == rhs.member_count &&
         lhs.epoch_authenticator == rhs.epoch_authenticator;
}

MLSClient::Epoch
MLSClient::next_epoch()
{
  return epochs.pop();
}

bool
MLSClient::should_commit(size_t n_adds,
                         const std::vector<mls::LeafIndex>& removed) const
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
    std::make_shared<SubDelegate>(logger, inbound_objects, std::move(promise));

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

  // XXX(RLB) `delegate` is destroyed at this point, because quicr::Client
  // doesn't hold strong references to its delegates.  As a result, though,
  // quicr::Client fails cleanly when its references are invalidated, and we
  // don't need anything further from the delegate.  So we can let it go.
  return future.get();
}

void
MLSClient::publish(const quicr::Name& name, bytes&& data)
{
  logger->Log("Publish, name=" + std::string(name) +
              " size=" + std::to_string(data.size()));
  client->publishNamedObject(name, 0, default_ttl_ms, false, std::move(data));
}

// TODO(RLB): Split this method into different methods invoked by different
// types of subscriber delegates.
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

      publish_commit(std::move(commit));
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
      publish_commit(std::move(commit));

      return;
    }

    case NamespaceConfig::Operation::commit: {
      logger->Log("Received Commit");

      if (!joined()) {
        logger->Log("Ignoring Commit; not joined to the group");
        return;
      }

      const auto epoch = uint64_t(third_name_value);
      auto& session = std::get<MLSSession>(mls_session);
      if (epoch != session.get_state().epoch()) {
        logger->Log("Ignoring Commit that is not for the current epoch (" +
                    std::to_string(epoch) +
                    " != " + std::to_string(session.get_state().epoch()) + ")");
        return;
      }

      // Record the committer's vote for themselves
      commit_votes.try_emplace(epoch);
      commit_votes.at(epoch).try_emplace(sender, 0);
      commit_votes.at(epoch).at(sender) += 1;

      // Add the commit to the cache
      commit_cache.try_emplace(epoch);
      commit_cache.at(epoch).insert_or_assign(sender, data);

      // If this is the first commit for this epoch...
      if (commit_cache.at(epoch).size() == 1) {
        // Record our own vote
        commit_votes.at(epoch).at(sender) += 1;

        // Broadcast it to others
        logger->Log("Sending CommitVote for epoch=" + std::to_string(epoch) +
                    " committer=" + std::to_string(sender));
        auto& session = std::get<MLSSession>(mls_session);
        auto vote =
          session.wrap_vote({ MLSSession::VoteType::commit, epoch, sender });
        const auto vote_name = namespaces.for_commit_vote(user_id, epoch);
        publish(vote_name, std::move(vote));
      }

      // Advance if we are able
      advance_if_quorum();
      return;
    }

    case NamespaceConfig::Operation::commit_vote: {
      logger->Log("Received CommitVote");

      if (!joined()) {
        logger->Log("Ignoring CommitVote; not joined to the group");
        return;
      }

      auto epoch = uint64_t(third_name_value);
      auto& session = std::get<MLSSession>(mls_session);
      if (epoch != session.get_state().epoch()) {
        logger->Log("Ignoring CommitVote; wrong epoch (" +
                    std::to_string(epoch) +
                    " != " + std::to_string(session.get_state().epoch()) + ")");
        return;
      }

      auto vote = session.unwrap_vote(data);
      if (vote.type != MLSSession::VoteType::commit) {
        logger->Log("Invalid CommitVote; wrong type");
        return;
      }

      if (vote.id != epoch) {
        logger->Log("Invalid CommitVote; wrong ID");
        return;
      }

      // Record the vote
      logger->Log("Recording vote for epoch=" + std::to_string(epoch) +
                  " committer=" + std::to_string(vote.vote));
      commit_votes.try_emplace(epoch);
      commit_votes.at(epoch).try_emplace(vote.vote, 0);
      commit_votes.at(epoch).at(vote.vote) += 1;

      // Advance if we are able
      advance_if_quorum();
      return;
    }

    // TODO(richbarn): Run a gotKey vote as well as a commit vote
    default:
      throw std::runtime_error("Illegal operation in name: " +
                               std::to_string(op));
  }
}

void
MLSClient::publish_commit(bytes&& commit_data)
{
  logger->Log("Voting for our own Commit");
  auto& session = std::get<MLSSession>(mls_session);
  const auto epoch = session.get_state().epoch();
  commit_votes.try_emplace(epoch);
  commit_votes.at(epoch).try_emplace(user_id, 0);
  commit_votes.at(epoch).at(user_id) += 1;

  logger->Log("Caching our own Commit");
  commit_cache.try_emplace(epoch);
  commit_cache.at(epoch).try_emplace(user_id, commit_data);

  logger->Log("Publishing Commit Message");
  const auto commit_name = namespaces.for_commit(user_id, epoch);
  publish(commit_name, std::move(commit_data));

  // This advances in the special case where there is one member in the group
  // (and thus we need no other votes)
  advance_if_quorum();
}

void
MLSClient::advance_if_quorum()
{
  logger->Log("Attempting to advance the MLS state...");
  auto& session = std::get<MLSSession>(mls_session);
  const auto epoch = session.get_state().epoch();
  const auto quorum = (session.member_count() / 2) + 1;

  if (!commit_votes.contains(epoch)) {
    logger->Log("Failed to advance; no votes for this epoch");
    return;
  }

  const auto& votes = commit_votes.at(epoch);
  const auto committer_it =
    std::find_if(votes.begin(), votes.end(), [&](const auto& pair) {
      return pair.second >= quorum;
    });

  if (committer_it == votes.end()) {
    // No quorum
    logger->Log("Failed to advance; No quorum");
    return;
  }

  const auto committer = committer_it->first;
  if (!commit_cache.at(epoch).contains(committer)) {
    // No commit cached
    logger->Log("Failed to advance; Commit is not yet available");
    return;
  }

  const auto& commit = commit_cache.at(epoch).at(committer);
  switch (session.handle(commit)) {
    case MLSSession::HandleResult::ok:
      epochs.push({ session.get_state().epoch(),
                    session.member_count(),
                    session.get_state().epoch_authenticator() });
      logger->Log("Updated to epoch " +
                  std::to_string(session.get_state().epoch()));
      break;

    case MLSSession::HandleResult::fail:
      logger->Log("Failed to advance; unspecified failure");
      break;

    case MLSSession::HandleResult::stale:
      logger->Log("Failed to advance; stale commit");
      break;

    case MLSSession::HandleResult::future:
      logger->Log("Failed to advance; future commit");
      break;

    case MLSSession::HandleResult::removes_me:
      logger->Log("Failed to advance; MLS commit would remove me");
      break;

    default:
      logger->Log("Failed to advance; reason unknown");
      return;
  }
}
