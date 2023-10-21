#include "lock.h"

#include <random>
#include <tls/tls_syntax.h>

namespace lock {

static DestroyToken
fresh_destroy_token()
{
  auto dist = std::uniform_int_distribution<uint64_t>(
    std::numeric_limits<uint64_t>::min(), std::numeric_limits<uint64_t>::max());
  auto engine = std::random_device();
  return mls::tls::marshal(dist(engine));
}

InMemoryService::InMemoryService(cantina::LoggerPointer logger_in)
  : logger(
      std::make_shared<cantina::Logger>("LockSvc", std::move(logger_in), true))
{
}

LockResponse
InMemoryService::acquire(const LockID& lock_id, Duration duration)
{
  const auto _ = lock();
  const auto now = clean_up_expired();

  // Check that the lock has not been destroyed
  if (destroyed_locks.contains(lock_id)) {
    logger->info << "Acquire lock_id=" << lock_id << " => Destroyed"
                 << std::flush;
    return Destroyed{};
  }

  // Check that the lock is not already locked
  if (acquired_locks.contains(lock_id)) {
    logger->info << "Acquire lock_id=" << lock_id << " => Locked" << std::flush;
    return Locked{ .expiry = acquired_locks.at(lock_id).expiry };
  }

  // Mark the lock as acquired
  const auto lock = Lock{
    .expiry = now + duration,
    .destroy_token = fresh_destroy_token(),
  };
  acquired_locks.insert_or_assign(lock_id, lock);
  logger->info << "Acquire lock_id=" << lock_id << " => AcquireOK"
               << std::flush;
  return AcquireOK{ .expiry = acquired_locks.at(lock_id).expiry,
                    .destroy_token = acquired_locks.at(lock_id).destroy_token };
}

DestroyResponse
InMemoryService::destroy(const LockID& lock_id,
                         const DestroyToken& destroy_token)
{
  const auto _ = lock();
  clean_up_expired();

  // Check that the lock has been acquired and the destroy_token is invalid
  if (!acquired_locks.contains(lock_id)) {
    logger->info << "Destroy lock_id=" << lock_id
                 << " => Unauthorized (not acquired)" << std::flush;
    return Unauthorized{};
  }

  if (acquired_locks.at(lock_id).destroy_token != destroy_token) {
    logger->info << "Destroy lock_id=" << lock_id << " => Unauthorized (["
                 << acquired_locks.at(lock_id).destroy_token << "] != ["
                 << destroy_token << "])" << std::flush;
    return Unauthorized{};
  }

  // Mark the lock as destroyed
  acquired_locks.erase(lock_id);
  destroyed_locks.insert(lock_id);
  logger->info << "Destroy lock_id=" << lock_id << " => DestroyOK"
               << std::flush;
  return DestroyOK{};
}

std::lock_guard<std::mutex>
InMemoryService::lock()
{
  return std::lock_guard(mutex);
}

TimePoint
InMemoryService::clean_up_expired()
{
  const auto now = std::chrono::system_clock::now();
  std::erase_if(acquired_locks,
                [&](const auto& pair) { return pair.second.expiry < now; });
  return now;
}

} // namespace lock
