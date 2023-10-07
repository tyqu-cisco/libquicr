#include "epoch_server.h"

#include <random>

namespace epoch {

using std::chrono::system_clock;

std::unique_lock<std::mutex>
InMemoryServer::lock()
{
  return std::unique_lock{ global_mutex };
}

TransactionID
InMemoryServer::random_transaction_id() const
{
  auto dist = std::uniform_int_distribution<TransactionID>(
    std::numeric_limits<TransactionID>::min(),
    std::numeric_limits<TransactionID>::max());
  auto engine = std::random_device();
  return dist(engine);
}

create_init::Response
InMemoryServer::create_init(GroupID group_id)
{
  const auto _ = lock();
  const auto now = system_clock::now();

  if (commit.contains(group_id)) {
    return create_init::Created{};
  }

  if (create.contains(group_id) && now < create.at(group_id).expiry) {
    // Timer still active
    return create_init::Conflict{ create.at(group_id).expiry };
  }

  // No entry, or expired timer, clear to create
  const auto transaction_id = random_transaction_id();
  const auto expiry = now + create_timeout;
  const auto txn = Transaction{ expiry, transaction_id };
  create.insert_or_assign(group_id, txn);

  return create_init::OK{ transaction_id };
}

create_complete::Response
InMemoryServer::create_complete(GroupID group_id, TransactionID tx_id)
{
  const auto _ = lock();
  const auto now = system_clock::now();

  if (commit.contains(group_id)) {
    // Already created somehow
    return create_complete::Created{};
  }

  if (!create.contains(group_id)) {
    // Complete without init
    return create_complete::InvalidTransaction{};
  }

  const auto& txn = create.at(group_id);
  if (txn.transaction_id != tx_id) {
    // Incorrect transaction ID
    return create_complete::InvalidTransaction{};
  }

  if (txn.expiry < now) {
    // Too late, entry expired
    return create_complete::InvalidTransaction{};
  }

  const auto group_state = GroupState{
    .epoch_id = 0,
    .pending_commit = std::nullopt,
  };
  create.erase(group_id);
  commit.insert_or_assign(group_id, group_state);

  return create_complete::OK{};
}

commit_init::Response
InMemoryServer::commit_init(GroupID group_id, EpochID epoch_id)
{
  const auto _ = lock();
  const auto now = system_clock::now();

  if (!commit.contains(group_id)) {
    return commit_init::UnknownGroup{};
  }

  auto& group_state = commit.at(group_id);
  if (epoch_id != group_state.epoch_id) {
    return commit_init::InvalidEpoch{ group_state.epoch_id };
  }

  if (group_state.pending_commit) {
    const auto expiry = group_state.pending_commit.value().expiry;
    if (now < expiry) {
      // Timer still active
      return commit_init::Conflict{ expiry };
    }
  }

  // No entry, or expired timer, clear to create
  const auto transaction_id = random_transaction_id();
  const auto expiry = now + commit_timeout;
  group_state.pending_commit = Transaction{ expiry, transaction_id };

  return commit_init::OK{ transaction_id };
}

commit_complete::Response
InMemoryServer::commit_complete(GroupID group_id,
                                EpochID epoch_id,
                                TransactionID tx_id)
{
  const auto _ = lock();
  const auto now = system_clock::now();

  if (!commit.contains(group_id)) {
    return commit_complete::UnknownGroup{};
  }

  auto& group_state = commit.at(group_id);
  if (epoch_id != group_state.epoch_id) {
    return commit_complete::InvalidEpoch{ group_state.epoch_id };
  }

  if (!group_state.pending_commit) {
    return commit_complete::InvalidTransaction{};
  }

  {
    // Scope the reference so that it is inaccessible by the time we invalidate
    // it by modifying the underlying optional.
    const auto& txn = group_state.pending_commit.value();
    if (txn.transaction_id != tx_id || txn.expiry < now) {
      return commit_complete::InvalidTransaction{};
    }
  }

  group_state.epoch_id += 1;
  group_state.pending_commit = std::nullopt;
  return commit_complete::OK{};
}

} // namespace epoch
