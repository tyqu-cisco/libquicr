#pragma once

#include <bytes/bytes.h>

#include <map>
#include <mutex>
#include <set>

namespace lock {

using ThingID = mls::bytes_ns::bytes;
using Duration = std::chrono::milliseconds;
using TimePoint = std::chrono::system_clock::time_point;

struct DurationTooLarge
{
  Duration max_duration;
};

struct Conflict
{
  TimePoint retry_after;
};

struct OK
{
  TimePoint expiry;
};

using Response = std::variant<DurationTooLarge, Conflict, OK>;

struct Service
{
  virtual ~Service() = default;

  // Lock service, could be ephemeral
  virtual Response lock(const ThingID& thing_id, Duration duration) = 0;

  // Existence service, needs to be durable
  virtual bool contains(const ThingID& thing_id) = 0;
  virtual void add(const ThingID& thing_id) = 0;

  // On connect:
  // if (!contains(group_id + 0)) {
  //   lock(group_id + 0, duration);
  //   // Create group
  //   add(group_id + 0);
  // }

  // On commit (in epoch e):
  // if (!contains(group_id + (e+1))) {
  //   lock(group_id + (e+1), duration);
  //   // Send Commit
  //   add(group_id + (e+1));
  // }
};

struct InMemoryService : Service
{
  static constexpr auto max_duration = std::chrono::milliseconds(60'000);

  Response lock(const ThingID& lock_id, Duration duration) override;
  bool contains(const ThingID& thing_id) override;
  void add(const ThingID& thing_id) override;

private:
  std::lock_guard<std::mutex> lock();
  std::mutex mutex;

  std::map<ThingID, TimePoint> locks;
  std::set<ThingID> things;
};

} // namespace lock
