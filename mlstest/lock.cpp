#include "lock.h"

namespace lock {

Response
InMemoryService::lock(const ThingID& thing_id, Duration duration)
{
  const auto _ = lock();

  // Clean up expired locks
  const auto now = std::chrono::system_clock::now();
  std::erase_if(locks, [&](const auto& pair) { return pair.second < now; });

  // Check that the duration is acceptable
  if (duration > max_duration) {
    return DurationTooLarge{ .max_duration = max_duration };
  }

  // Chack that the lock is not already locked
  if (locks.contains(thing_id)) {
    return Conflict{ .retry_after = locks.at(thing_id) };
  }

  const auto expiry = now + duration;
  locks.insert_or_assign(thing_id, expiry);
  return OK{ .expiry = expiry };
}

bool
InMemoryService::contains(const ThingID& thing_id)
{
  const auto _ = lock();
  return things.contains(thing_id);
}

void
InMemoryService::add(const ThingID& thing_id)
{
  const auto _ = lock();
  things.insert(thing_id);
}

std::lock_guard<std::mutex>
InMemoryService::lock()
{
  return std::lock_guard(mutex);
}

} // namespace lock
