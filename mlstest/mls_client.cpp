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

  // XXX(richbarn) These subscriptions / publishes are done serially; we await a
  // response for each one before doing the next.  They could be done in
  // parallel by having subscribe/publish_intent return std::future<bool> and
  // awaiting all of these futures together.

  // Subscribe to the required namespaces
  auto success = true;
  success = success && subscribe(namespaces.key_package_sub());
  if (!as_creator) {
    success = success && subscribe(namespaces.welcome_sub());
  }
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
  handler_thread = std::thread([&]() {
    while (!stop_threads) {
      auto maybe_obj = inbound_objects->pop(inbound_object_timeout);
      if (!maybe_obj) {
        continue;
      }

      const auto _ = lock();
      auto& obj = maybe_obj.value();
      handle(std::move(obj));
    }

    logger->Log("Handler thread stopping");
  });

  // Start up a thread to commit requests from other clients
  commit_thread = std::thread([&]() {
    while (!stop_threads) {
      std::this_thread::sleep_for(commit_interval);

      const auto _ = lock();
      make_commit();
    }
  });

  return true;
}

void
MLSClient::disconnect()
{
  logger->Log("Disconnecting QuicR client");
  client->disconnect();

  stop_threads = true;

  if (handler_thread && handler_thread.value().joinable()) {
    logger->Log("Stopping handler thread");
    handler_thread.value().join();
    logger->Log("Handler thread stopped");
  }

  logger->Log("Stopping commit thread");
  if (commit_thread && commit_thread.value().joinable()) {
    commit_thread.value().join();
    logger->Log("Commit thread stopped");
  }
}

std::future<bool>
MLSClient::join()
{
  const auto _ = lock();
  const auto& kp = std::get<MLSInitInfo>(mls_session).key_package;
  const auto kp_id = NamespaceConfig::id_for(kp);
  const auto name = namespaces.for_key_package(user_id, kp_id);

  join_promise = std::promise<bool>();
  auto join_future = join_promise->get_future();

  publish(name, tls::marshal(kp));

  return join_future;
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

MLSClient::Epoch
MLSClient::latest_epoch()
{
  auto epoch = epochs.pop();
  while (!epochs.empty()) {
    epoch = epochs.pop();
  }
  return epoch;
}

bool
MLSClient::should_commit(size_t n_adds,
                         const std::vector<ParsedLeaveRequest>& leaves) const
{
  // TODO(richbarn): This method should be sensitive to what is being committed.
  // For example, the tree stays maximally full if the neighbor of a removed
  // node commits the remove.
  return session().should_commit(n_adds, leaves);
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
MLSClient::handle(QuicrObject&& obj)
{
  const auto [op, sender, third_name_value] = namespaces.parse(obj.name);

  // Any MLSMesssage-formatted messages that are for a future epoch get
  // enqueued for later processing.
  switch (op) {
    case NamespaceConfig::Operation::leave:
      [[fallthrough]];
    case NamespaceConfig::Operation::commit:
      [[fallthrough]];
    case NamespaceConfig::Operation::commit_vote: {
      const auto* session = std::get_if<MLSSession>(&mls_session);
      if (!session || session->future(obj.data)) {
        future_epoch_objects.push_back(std::move(obj));
        return;
      }

      break;
    }

    case NamespaceConfig::Operation::key_package:
      [[fallthrough]];
    case NamespaceConfig::Operation::welcome: {
      break;
    }

    default:
      throw std::runtime_error("Illegal operation in name: " +
                               std::to_string(op));
  }

  // Handle objects according to type
  switch (op) {
    case NamespaceConfig::Operation::key_package: {
      logger->Log("Received Join, sender=" + std::to_string(sender));
      auto parsed = MLSSession::parse_join(obj.data);
      if (parsed.user_id != sender) {
        logger->Log("Invalid join request; user ID mismatch");
        return;
      }

      const auto kp_id = NamespaceConfig::id_for(parsed.key_package);
      if (kp_id != third_name_value) {
        logger->Log("Invalid join request; key package mismatch");
        return;
      }

      joins_to_commit.push_back(std::move(parsed));
      return;
    }

    case NamespaceConfig::Operation::leave: {
      logger->Log("Received Leave, sender=" + std::to_string(sender));

      if (!joined()) {
        logger->Log("Ignoring leave request; not joined to the group");
        return;
      }

      auto& session = std::get<MLSSession>(mls_session);
      auto maybe_parsed = session.parse_leave(obj.data);
      if (!maybe_parsed) {
        logger->Log("Ignoring leave request; unable to process");
        return;
      }

      auto& parsed = maybe_parsed.value();
      if (parsed.user_id != sender) {
        logger->Log("Ignoring leave request; user ID mismatch");
      }

      leaves_to_commit.push_back(std::move(parsed));
      return;
    }

    case NamespaceConfig::Operation::welcome: {
      logger->Log("Received Welcome");

      if (joined()) {
        logger->Log("Ignoring Welcome; already joined to the group");
        return;
      }

      // Join the group
      const auto& init_info = std::get<MLSInitInfo>(mls_session);
      const auto maybe_mls_session = MLSSession::join(init_info, obj.data);
      if (!maybe_mls_session) {
        logger->Log("Ignoring Welcome; not for me");
        return;
      }

      mls_session = maybe_mls_session.value();
      if (join_promise) {
        join_promise->set_value(true);
      }

      auto& session = std::get<MLSSession>(mls_session);
      epochs.push({ session.get_state().epoch(),
                    session.member_count(),
                    session.get_state().epoch_authenticator() });

      const auto welcome_ns = namespaces.welcome_sub();
      client->unsubscribe(welcome_ns, "bogus_origin_url", "bogus_auth_token");

      // Request an empty commit to populate my path in the tree
      const auto index = session.get_state().index();
      old_leaf_node_to_commit =
        session.get_state().tree().leaf_node(index).value();

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
      commit_cache.at(epoch).insert_or_assign(sender, obj.data);

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

      auto vote = session.unwrap_vote(obj.data);
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

    default:
      throw std::runtime_error("Illegal operation in name: " +
                               std::to_string(op));
  }
}

void
MLSClient::make_commit()
{
  // Can't commit if we're not joined
  if (!joined()) {
    return;
  }

  auto& session = std::get<MLSSession>(mls_session);

  // Don't create more than one commit per epoch
  if (pending_commit) {
    return;
  }

  // Import the requests
  groom_request_queues();

  const auto self_update = old_leaf_node_to_commit.has_value();
  const auto& joins = joins_to_commit;
  const auto& leaves = leaves_to_commit;

  // Compute the set of names to send a welcome to (if any)
  auto welcome_names = std::vector<quicr::Name>{};
  std::transform(joins.begin(),
                 joins.end(),
                 std::back_inserter(welcome_names),
                 [&](const auto& join) {
                   const auto kp_id = NamespaceConfig::id_for(join.key_package);
                   return namespaces.for_welcome(user_id, kp_id);
                 });

  // Abort if nothing to commit
  if (!self_update && joins.empty() && leaves.empty()) {
    logger->Log("Not committing; nothing to commit");
    return;
  }

  // XXX(richbarn): In this ad-hoc batched mode, this is no longer reliable.
  // Replace with a time-based solution.
  if (!self_update && !should_commit(joins.size(), leaves)) {
    logger->Log("Not committing; not the designated committer");
    return;
  }

  logger->info << "Committing Join=#" << joins.size()
               << " SelfUpdate=" << (self_update ? "Y" : "N") << " Leave=#"
               << leaves.size() << std::flush;
  auto [commit, welcome] = session.commit(self_update, joins, leaves);

  pending_commit = PendingCommit{
    .commit = commit,
    .welcome = welcome,
    .welcome_names = welcome_names,
  };

  // Publish the commit
  const auto epoch = session.get_state().epoch();
  commit_votes.try_emplace(epoch);
  commit_votes.at(epoch).try_emplace(user_id, 0);
  commit_votes.at(epoch).at(user_id) += 1;

  commit_cache.try_emplace(epoch);
  commit_cache.at(epoch).try_emplace(user_id, commit);

  const auto commit_name = namespaces.for_commit(user_id, epoch);
  publish(commit_name, std::move(commit));

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

  // XXX(richbarn) This is wrong, it should be (n/2)+1.  It has been reduced by
  // one so that we don't hang in the case where the last member is being
  // removed from a two-member group.
  const auto quorum = (session.member_count() / 2);

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

  // If this was a commit we created and there is a corresponding Welcome,
  // release the Welcome to the new joiners
  if (pending_commit && pending_commit.value().commit == commit) {
    const auto& pc = pending_commit.value();
    for (const auto& name : pc.welcome_names) {
      auto welcome = pc.welcome;
      publish(name, std::move(welcome));
    }
  }

  pending_commit.reset();

  // Groom the request queues, removing any requests that are obsolete
  groom_request_queues();

  // Handle any out-of-order messages that have been enqueued
  for (auto& obj : future_epoch_objects) {
    if (!session.current(obj.data)) {
      continue;
    }

    handle(std::move(obj));
  }

  std::erase_if(future_epoch_objects,
                [&](const auto& obj) { return session.current(obj.data); });
}

void
MLSClient::groom_request_queues()
{
  const auto& session = std::get<MLSSession>(mls_session);
  const auto obsolete = [session](const auto& req) { return session.obsolete(req); };
  std::erase_if(joins_to_commit, obsolete);
  std::erase_if(leaves_to_commit, obsolete);

  if (old_leaf_node_to_commit) {
    // A self-update request is obsolete if the old leaf node no longer appears
    const auto& leaf = old_leaf_node_to_commit.value();
    if (!session.get_state().tree().find(leaf)) {
      old_leaf_node_to_commit.reset();
    }
  }
}
