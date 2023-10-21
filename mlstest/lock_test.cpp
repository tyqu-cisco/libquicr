#include <doctest/doctest.h>

#include "lock.h"

#include <thread>

using namespace lock;
using namespace std::chrono_literals;

TEST_CASE("In-memory epoch server")
{
  const auto logger = std::make_shared<cantina::Logger>(true);
  auto lock_service = std::make_shared<InMemoryService>(logger);

  auto lock_id = std::vector<uint8_t>{ 1, 2, 3, 4, 0 };
  const auto lock_duration = 100ms;

  SUBCASE("Happy Path")
  {
    const auto max_epoch = uint8_t(10);

    // Create the group
    const auto acquire_resp = lock_service->acquire(lock_id, lock_duration);
    REQUIRE(std::holds_alternative<AcquireOK>(acquire_resp));

    const auto ok = std::get<AcquireOK>(acquire_resp);
    const auto destroy_resp = lock_service->destroy(lock_id, ok.destroy_token);
    REQUIRE(std::holds_alternative<DestroyOK>(destroy_resp));

    // Commit to the group
    for (auto epoch_id = 1; epoch_id < max_epoch; epoch_id++) {
      lock_id[4] += epoch_id;

      const auto acquire_resp = lock_service->acquire(lock_id, lock_duration);
      REQUIRE(std::holds_alternative<AcquireOK>(acquire_resp));

      const auto ok = std::get<AcquireOK>(acquire_resp);
      const auto destroy_resp = lock_service->destroy(lock_id, ok.destroy_token);
      REQUIRE(std::holds_alternative<DestroyOK>(destroy_resp));
    }
  }

  SUBCASE("Acquired locks block until they expire")
  {
    // Acquire the lock
    const auto acquire_resp_1 = lock_service->acquire(lock_id, lock_duration);
    REQUIRE(std::holds_alternative<AcquireOK>(acquire_resp_1));

    // Attempting to acquire the lock again should fail
    const auto acquire_resp_2 = lock_service->acquire(lock_id, lock_duration);
    REQUIRE(std::holds_alternative<Locked>(acquire_resp_2));

    // Wait for the lock to expire
    const auto expiry = std::get<Locked>(acquire_resp_2).expiry;
    std::this_thread::sleep_until(expiry);

    // We should now be able to acquire the lock
    const auto acquire_resp_3 = lock_service->acquire(lock_id, lock_duration);
    REQUIRE(std::holds_alternative<AcquireOK>(acquire_resp_3));
  }

  SUBCASE("A lock can be destroyed only while acquired")
  {
    // Acquire the lock
    const auto acquire_resp_1 = lock_service->acquire(lock_id, lock_duration);
    REQUIRE(std::holds_alternative<AcquireOK>(acquire_resp_1));

    // Allow the lock to expire
    const auto& [expiry_1, destroy_token_1] = std::get<AcquireOK>(acquire_resp_1);
    std::this_thread::sleep_until(expiry_1);

    // Attempting to destroy the lock should fail
    const auto destroy_resp_1 = lock_service->destroy(lock_id, destroy_token_1);
    REQUIRE(std::holds_alternative<Unauthorized>(destroy_resp_1));

    // Re-acquire the lock
    const auto acquire_resp_2 = lock_service->acquire(lock_id, lock_duration);
    REQUIRE(std::holds_alternative<AcquireOK>(acquire_resp_2));

    // Now we should be able to destroy the lock
    const auto& destroy_token_2 = std::get<AcquireOK>(acquire_resp_2).destroy_token;
    REQUIRE(destroy_token_2 != destroy_token_1);

    const auto destroy_resp_2 = lock_service->destroy(lock_id, destroy_token_2);
    REQUIRE(std::holds_alternative<DestroyOK>(destroy_resp_2));
  }

  SUBCASE("Destroyed locks cannot be acquired")
  {
    // Acquire and destroy the lock
    const auto acquire_resp_1 = lock_service->acquire(lock_id, lock_duration);
    REQUIRE(std::holds_alternative<AcquireOK>(acquire_resp_1));

    const auto& destroy_token_1 = std::get<AcquireOK>(acquire_resp_1).destroy_token;
    const auto destroy_resp_1 = lock_service->destroy(lock_id, destroy_token_1);
    REQUIRE(std::holds_alternative<DestroyOK>(destroy_resp_1));

    // Attempting to acquire the lock should now fail
    const auto acquire_resp_2 = lock_service->acquire(lock_id, lock_duration);
    REQUIRE(std::holds_alternative<Destroyed>(acquire_resp_2));
  }
}
