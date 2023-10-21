#pragma once

#include <bytes/bytes.h>
#include <cantina/logger.h>

#include <map>
#include <mutex>
#include <set>

namespace lock {

using LockID = mls::bytes_ns::bytes;
using DestroyToken = mls::bytes_ns::bytes;
using Duration = std::chrono::milliseconds;
using TimePoint = std::chrono::system_clock::time_point;

struct Locked
{
  TimePoint expiry;
};

struct Destroyed
{};

struct Unauthorized
{};

struct AcquireOK
{
  TimePoint expiry;
  DestroyToken destroy_token;
};

struct DestroyOK
{};

using LockResponse = std::variant<Locked, Destroyed, AcquireOK>;
using DestroyResponse = std::variant<Unauthorized, DestroyOK>;

// The lock service tracks locks according to unique identifiers.  It is up to
// the caller to assure uniqueness of identifiers.  You can do two things with a
// lock, acquire it or destroy it.
//
// When you acquire the lock, you lock it temporarily.  After a specified
// expiration interval, the lock will be released, and other clients can acquire
// it.  When you successfully acquire the lock, you are given a token that you
// can use to destroy it.
//
// When you destroy the lock, you lock it permanently.  No other client will be
// able to acquire the lock, ever.  You can only destroy the lock while you hold
// it.
//
// This two-phase structure provides the minimal synchroniazation that MLS
// requires.  Group members form lock IDs as (group_id, epoch).  A group member
// attempting to create the group will attempt to lock (group_id, 0), and
// destroy it once the group is created.  A committer will attempt to lock
// (group_id, epoch + 1) and destroy it once the commit is distributed.  In
// other words, group members signal that the group has moved into `epoch` by
// destroying the lock for (group_id, epoch).
//
// In principle, the `destroy` function requires that the lock service maintain
// unbounded state.  In practice, there will probably be ways to clean up this
// state over time, but we leave that for future work right now.
struct Service
{
  virtual ~Service() = default;

  // Acquire the lock.
  //
  // Responses:
  // * Locked: The lock is already locked
  // * Destroyed: The lock has already been destroyed
  // * LockOK: The caller now owns the lock
  virtual LockResponse acquire(const LockID& lock_id, Duration duration) = 0;

  // Destroy the lock.
  //
  // Responses:
  // * Unauthorized: The provided destroy token is invalid for this lock
  // * DestroyOK: The lock has been destroyed
  virtual DestroyResponse destroy(const LockID& lock_id,
                                  const DestroyToken& destroy_token) = 0;
};

struct InMemoryService : Service
{
  InMemoryService(cantina::LoggerPointer logger_in);

  LockResponse acquire(const LockID& lock_id, Duration duration) override;
  DestroyResponse destroy(const LockID& lock_id,
                          const DestroyToken& destroy_token) override;

private:
  cantina::LoggerPointer logger;

  std::lock_guard<std::mutex> lock();
  std::mutex mutex;

  struct Lock
  {
    TimePoint expiry;
    DestroyToken destroy_token;
  };

  std::map<LockID, Lock> acquired_locks;
  std::set<LockID> destroyed_locks;

  TimePoint clean_up_expired();
};

} // namespace lock
