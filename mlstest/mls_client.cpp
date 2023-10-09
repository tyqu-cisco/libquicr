#include "mls_client.h"
#include "pub_delegate.h"
#include "sub_delegate.h"

#include <transport/transport.h>

using namespace mls;
using namespace std::chrono_literals;

static const size_t epochs_capacity = 100;

MLSClient::MLSClient(const Config& config)
  : logger(config.logger)
  , group_id(config.group_id)
  , user_id(config.user_id)
  , epoch_sync_service(config.epoch_sync_service)
  , delivery_service(config.delivery_service)
  , mls_session(MLSInitInfo{ suite, user_id })
  , epochs(epochs_capacity)
{}

MLSClient::~MLSClient()
{
  disconnect();
}

bool
MLSClient::maybe_create_session()
{
  using namespace epoch_sync;
  static const auto invalid_tx_retry = std::chrono::milliseconds(100);

  // Get permission to create the group
  const auto init_resp = epoch_sync_service->create_init(group_id);
  if (std::holds_alternative<create_init::Created>(init_resp)) {
    return false;
  }

  if (std::holds_alternative<create_init::Conflict>(init_resp)) {
    const auto& conflict = std::get<create_init::Conflict>(init_resp);
    std::this_thread::sleep_until(conflict.retry_after);
    return maybe_create_session();
  }

  const auto& init_ok = std::get<create_init::OK>(init_resp);
  const auto tx_id = init_ok.transaction_id;

  // Create the group
  const auto init_info = std::get<MLSInitInfo>(mls_session);
  const auto session = MLSSession::create(init_info, group_id);

  // Report that the group has been created
  const auto complete_resp = epoch_sync_service->create_complete(group_id, tx_id);
  if (std::holds_alternative<create_complete::Created>(complete_resp)) {
    return false;
  }

  if (std::holds_alternative<create_complete::InvalidTransaction>(
        complete_resp)) {
    std::this_thread::sleep_for(invalid_tx_retry);
    return maybe_create_session();
  }

  // Install the group
  mls_session = session;
  return true;
}

bool
MLSClient::connect()
{
  // Determine whether to create the group
  auto as_creator = maybe_create_session();

  // Connect to the delivery service
  if (!delivery_service->connect(as_creator)) {
    return false;
  }

  // Start up a thread to handle incoming messages
  handler_thread = std::thread([&]() {
    while (!stop_threads) {
      auto maybe_obj = delivery_service->inbound_objects.receive(inbound_object_timeout);
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
  logger->Log("Disconnecting delivery service");
  delivery_service->disconnect();

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
  // TODO(richbarn) Optimize out the need for the client to compute this ID
  const auto kp_id = NamespaceConfig::id_for(kp);

  join_promise = std::promise<bool>();
  delivery_service->join_request(kp_id, tls::marshal(kp));

  return join_promise->get_future();
}

void
MLSClient::leave()
{
  auto self_remove = std::get<MLSSession>(mls_session).leave();
  delivery_service->leave_request(std::move(self_remove));

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
  return epochs.receive().value();
}

MLSClient::Epoch
MLSClient::latest_epoch()
{
  auto epoch = epochs.receive().value();
  while (!epochs.is_empty()) {
    epoch = epochs.receive().value();
  }
  return epoch;
}

void
MLSClient::handle(delivery::Object&& obj)
{
  const auto op = obj.op;
  const auto sender = obj.sender;
  const auto third_name_value = obj.third_name_value;

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

      joins_to_commit.push_back(defer(std::move(parsed)));
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

      leaves_to_commit.push_back(defer(std::move(parsed)));
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
      epochs.send({ session.get_state().epoch(),
                    session.member_count(),
                    session.get_state().epoch_authenticator() });

      // TODO(richbarn): Re-enable unsubscribing from welcome messages once
      // joined.  Need to figure out how to do this within the DS framework.
      //const auto welcome_ns = namespaces.welcome_sub();
      //client->unsubscribe(welcome_ns, "bogus_origin_url", "bogus_auth_token");

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

      advance(obj.data);
      return;
    }

    default:
      throw std::runtime_error("Illegal operation in name: " +
                               std::to_string(op));
  }
}

MLSClient::TimePoint
MLSClient::not_before(uint32_t distance)
{
  auto now = std::chrono::system_clock::now();
  return now + distance * commit_delay_unit;
}

MLSClient::Deferred<ParsedJoinRequest>
MLSClient::defer(ParsedJoinRequest&& join)
{
  const auto& session = std::get<MLSSession>(mls_session);
  const auto distance = session.distance_from(joins_to_commit.size(), {});
  return { not_before(distance), std::move(join) };
}

MLSClient::Deferred<ParsedLeaveRequest>
MLSClient::defer(ParsedLeaveRequest&& leave)
{
  const auto& session = std::get<MLSSession>(mls_session);
  const auto distance = session.distance_from(0, { leave });
  return { not_before(distance), std::move(leave) };
}

void
MLSClient::make_commit()
{
  using namespace epoch_sync;

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

  // Select the requests for which a commit is timely
  const auto self_update = old_leaf_node_to_commit.has_value();

  const auto now = std::chrono::system_clock::now();
  auto joins = std::vector<ParsedJoinRequest>{};
  for (const auto& deferred : joins_to_commit) {
    if (deferred.not_before < now) {
      joins.push_back(deferred.request);
    }
  }

  auto leaves = std::vector<ParsedLeaveRequest>{};
  for (const auto& deferred : leaves_to_commit) {
    if (deferred.not_before < now) {
      leaves.push_back(deferred.request);
    }
  }

  // Abort if nothing to commit
  if (!self_update && joins.empty() && leaves.empty()) {
    logger->Log("Not committing; nothing to commit");
    return;
  }

  // Construct the commit
  logger->info << "Committing Join=[";
  for (const auto& join : joins) {
    logger->info << join.user_id << ",";
  }
  logger->info << "] SelfUpdate=" << (self_update ? "Y" : "N") << " Leave=[";
  for (const auto& leave : leaves) {
    logger->info << leave.user_id << ",";
  }
  logger->info << "]" << std::flush;
  auto [commit, welcome] = session.commit(self_update, joins, leaves);

  // Get permission to send a commit
  const auto epoch = session.get_state().epoch();
  const auto init_resp = epoch_sync_service->commit_init(group_id, epoch);
  if (std::holds_alternative<commit_init::InvalidEpoch>(init_resp)) {
    const auto server_epoch =
      std::get<commit_init::InvalidEpoch>(init_resp).current_epoch;
    logger->info << "Failed to initiate - epoch mismatch - mine=" << epoch
                 << " server=" << server_epoch << std::flush;
    return;
  }

  if (!std::holds_alternative<commit_init::OK>(init_resp)) {
    // Permission denied for some other reason
    logger->info << "Failed to initiate commit code=" << init_resp.index()
                 << std::flush;
    return;
  }

  const auto& init_ok = std::get<commit_init::OK>(init_resp);
  const auto tx_id = init_ok.transaction_id;

  // Publish the commit and update our own state
  delivery_service->commit(epoch, commit);

  // Inform the epoch server that the commit has been sent
  const auto complete_resp =
    epoch_sync_service->commit_complete(group_id, epoch, tx_id);
  if (!std::holds_alternative<commit_complete::OK>(complete_resp)) {
    // Something went wrong, abort and hope everyone ignores the commit
    logger->info << "Failed to complete commit code=" << complete_resp.index()
                 << std::flush;
    return;
  }

  // Publish the Welcome and update our own state now that everything is OK
  advance(commit);

  for (const auto& join : joins) {
    const auto kp_id = NamespaceConfig::id_for(join.key_package);
    delivery_service->welcome(kp_id, welcome);
  }
}

void
MLSClient::advance(const bytes& commit)
{
  logger->Log("Attempting to advance the MLS state...");

  // Apply the commit
  auto& session = std::get<MLSSession>(mls_session);
  switch (session.handle(commit)) {
    case MLSSession::HandleResult::ok:
      epochs.send({ session.get_state().epoch(),
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
  const auto obsolete = [session](const auto& deferred) {
    return session.obsolete(deferred.request);
  };

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
