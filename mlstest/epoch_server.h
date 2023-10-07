#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <string>

namespace epoch {

using GroupID = uint64_t;
using EpochID = uint64_t;
using TransactionID = uint64_t;
using TimePoint = std::chrono::time_point<std::chrono::system_clock>;

namespace create_init {
// The group already exists
struct Created
{};

// Group creation is in progress, please retry
struct Conflict
{
  TimePoint retry_after;
};

// The group does not exist, OK to create it
struct OK
{
  TransactionID transaction_id;
};

using Response = std::variant<Created, Conflict, OK>;
} // namespace create_init

namespace create_complete {
// The group already exists
struct Created
{};

// The presented transaction ID is invalid for this group
struct InvalidTransaction
{};

// The group has been created
struct OK
{};

using Response = std::variant<Created, InvalidTransaction, OK>;
} // namespace create_complete

namespace commit_init {
// The specified group does not exist
struct UnknownGroup
{};

// The specified epoch is the current epoch for this group
struct InvalidEpoch
{
  EpochID current_epoch;
};

// A Commit is in progress, please retry
struct Conflict
{
  TimePoint retry_after;
};

// You have clearance to send a Commit
struct OK
{
  TransactionID transaction_id;
};

using Response = std::variant<UnknownGroup, InvalidEpoch, Conflict, OK>;
} // namespace commit_init

namespace commit_complete {
// The specified group does not exist
struct UnknownGroup
{};

// The specified epoch is the current epoch for this group
struct InvalidEpoch
{
  EpochID current_epoch;
};

// The presented transaction ID is invalid for this group+epoch
struct InvalidTransaction
{};

// The commit has been accepted
struct OK
{};

using Response =
  std::variant<UnknownGroup, InvalidEpoch, InvalidTransaction, OK>;
} // namespace commit_complete

struct Server
{
  virtual ~Server() = default;

  virtual create_init::Response create_init(GroupID group_id) = 0;

  virtual create_complete::Response create_complete(GroupID group_id,
                                                    TransactionID tx_id) = 0;

  virtual commit_init::Response commit_init(GroupID group_id,
                                            EpochID epoch_id) = 0;

  virtual commit_complete::Response commit_complete(GroupID group_id,
                                                    EpochID epoch_id,
                                                    TransactionID tx_id) = 0;
};

struct InMemoryServer : Server
{
  static constexpr auto create_timeout = std::chrono::milliseconds(200);
  static constexpr auto commit_timeout = std::chrono::milliseconds(500);

  create_init::Response create_init(GroupID group_id) override;

  create_complete::Response create_complete(GroupID group_id,
                                            TransactionID tx_id) override;

  commit_init::Response commit_init(GroupID group_id,
                                    EpochID epoch_id) override;

  commit_complete::Response commit_complete(GroupID group_id,
                                            EpochID epoch_id,
                                            TransactionID tx_id) override;

private:
  struct Transaction
  {
    TimePoint expiry;
    TransactionID transaction_id = 0;
  };

  struct GroupState
  {
    EpochID epoch_id = 0;
    std::optional<Transaction> pending_commit;
  };

  std::mutex global_mutex;
  std::map<GroupID, Transaction> create;
  std::map<GroupID, GroupState> commit;

  std::unique_lock<std::mutex> lock();
  TransactionID random_transaction_id() const;
};

} // namespace epoch
