#include <doctest/doctest.h>

#include "counter.h"

#include <thread>

using namespace counter;
using namespace std::chrono_literals;

TEST_CASE("In-memory counter service")
{
  const auto logger = std::make_shared<cantina::Logger>(true);
  auto counter_service = std::make_shared<InMemoryService>(logger);

  auto counter_id = std::vector<uint8_t>{ 1, 2, 3, 4 };
  const auto lock_duration = 100ms;

  SUBCASE("Happy path")
  {
    const auto max_epoch = uint8_t(10);

    for (auto epoch_id = Counter(0); epoch_id < max_epoch; epoch_id++) {
      const auto lock_resp =
        counter_service->lock(counter_id, epoch_id, lock_duration);
      REQUIRE(std::holds_alternative<LockOK>(lock_resp));

      const auto ok = std::get<LockOK>(lock_resp);
      const auto increment_resp = counter_service->increment(ok.lock_id);
      REQUIRE(std::holds_alternative<IncrementOK>(increment_resp));
    }
  }

  SUBCASE("Acquired counter locks block until they expire")
  {
    // Acquire the lock
    const auto lock_resp_1 =
      counter_service->lock(counter_id, 0, lock_duration);
    REQUIRE(std::holds_alternative<LockOK>(lock_resp_1));

    // Attempting to lock the lock again should fail
    const auto lock_resp_2 =
      counter_service->lock(counter_id, 0, lock_duration);
    REQUIRE(std::holds_alternative<Locked>(lock_resp_2));

    // Wait for the lock to expire
    const auto expiry = std::get<Locked>(lock_resp_2).expiry;
    std::this_thread::sleep_until(expiry);

    // We should now be able to lock the lock
    const auto lock_resp_3 =
      counter_service->lock(counter_id, 0, lock_duration);
    REQUIRE(std::holds_alternative<LockOK>(lock_resp_3));
  }

  SUBCASE("A counter can be incremented only by the holder of the lock")
  {
    // Acquire the lock
    const auto lock_resp = counter_service->lock(counter_id, 0, lock_duration);
    REQUIRE(std::holds_alternative<LockOK>(lock_resp));

    // Attempting to increment with an invalid token should fail
    const auto increment_resp = counter_service->increment({});
    REQUIRE(std::holds_alternative<Unauthorized>(increment_resp));
  }

  SUBCASE("A counter can be incremented only while locked")
  {
    // Acquire the lock
    const auto lock_resp_1 =
      counter_service->lock(counter_id, 0, lock_duration);
    REQUIRE(std::holds_alternative<LockOK>(lock_resp_1));

    // Allow the lock to expire
    const auto& ok_1 = std::get<LockOK>(lock_resp_1);
    std::this_thread::sleep_until(ok_1.expiry);

    // Attempting to destroy the lock should fail
    const auto increment_resp_1 = counter_service->increment(ok_1.lock_id);
    REQUIRE(std::holds_alternative<Unauthorized>(increment_resp_1));

    // Re-acquire the lock
    const auto lock_resp_2 =
      counter_service->lock(counter_id, 0, lock_duration);
    REQUIRE(std::holds_alternative<LockOK>(lock_resp_2));

    // Now we should be able to destroy the lock
    const auto& ok_2 = std::get<LockOK>(lock_resp_2);
    REQUIRE(ok_1.lock_id != ok_2.lock_id);

    const auto increment_resp_2 = counter_service->increment(ok_2.lock_id);
    REQUIRE(std::holds_alternative<IncrementOK>(increment_resp_2));
  }

  SUBCASE("A counter can be incremented only be a client that is in sync")
  {
    // Initialize the counter (increment to zero)
    const auto lock_resp_1 =
      counter_service->lock(counter_id, 0, lock_duration);
    REQUIRE(std::holds_alternative<LockOK>(lock_resp_1));

    const auto& ok_1 = std::get<LockOK>(lock_resp_1);
    const auto increment_resp_1 = counter_service->increment(ok_1.lock_id);
    REQUIRE(std::holds_alternative<IncrementOK>(increment_resp_1));

    // Attempting to initialize the counter again should fail
    const auto lock_resp_2 =
      counter_service->lock(counter_id, 0, lock_duration);
    REQUIRE(std::holds_alternative<OutOfSync>(lock_resp_2));
  }
}
