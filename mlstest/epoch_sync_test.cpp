#include <doctest/doctest.h>

#include "epoch_sync.h"

#include <thread>

using namespace epoch_sync;

TEST_CASE("In-memory epoch server")
{
  static constexpr auto group_id = GroupID(42);
  auto epoch_server = std::make_shared<InMemoryServer>();

  SUBCASE("Happy Path")
  {
    static constexpr auto max_epoch = EpochID(10);

    // Create the group
    const auto create_init_resp = epoch_server->create_init(group_id);
    REQUIRE(std::holds_alternative<create_init::OK>(create_init_resp));

    const auto ok = std::get<create_init::OK>(create_init_resp);
    const auto create_complete_resp =
      epoch_server->create_complete(group_id, ok.transaction_id);
    REQUIRE(std::holds_alternative<create_complete::OK>(create_complete_resp));

    // Commit to the group
    for (auto epoch_id = EpochID(0); epoch_id < max_epoch; epoch_id++) {
      const auto commit_init_resp =
        epoch_server->commit_init(group_id, epoch_id);
      REQUIRE(std::holds_alternative<commit_init::OK>(commit_init_resp));

      const auto commit_tx_id =
        std::get<commit_init::OK>(commit_init_resp).transaction_id;

      const auto commit_complete_resp =
        epoch_server->commit_complete(group_id, epoch_id, commit_tx_id);
      REQUIRE(
        std::holds_alternative<commit_complete::OK>(commit_complete_resp));
    }
  }

  SUBCASE("Create Conflict")
  {
    // Start the creation process
    const auto init_resp1 = epoch_server->create_init(group_id);
    REQUIRE(std::holds_alternative<create_init::OK>(init_resp1));

    // Attempting to start again should fail
    const auto init_resp2 = epoch_server->create_init(group_id);
    REQUIRE(std::holds_alternative<create_init::Conflict>(init_resp2));

    // Wait until the timer expires
    const auto conflict = std::get<create_init::Conflict>(init_resp2);
    std::this_thread::sleep_until(conflict.retry_after);

    // Attempting to complete the original transaction should now fail
    const auto ok1 = std::get<create_init::OK>(init_resp1);
    const auto complete_resp =
      epoch_server->create_complete(group_id, ok1.transaction_id);
    REQUIRE(std::holds_alternative<create_complete::InvalidTransaction>(
      complete_resp));

    // Starting fresh, we should now be able to create the group
    const auto init_resp4 = epoch_server->create_init(group_id);
    REQUIRE(std::holds_alternative<create_init::OK>(init_resp4));

    const auto ok2 = std::get<create_init::OK>(init_resp4);
    const auto complete_resp2 =
      epoch_server->create_complete(group_id, ok2.transaction_id);
    REQUIRE(std::holds_alternative<create_complete::OK>(complete_resp2));

    // After creation, requests to create should fail successfully
    const auto init_resp5 = epoch_server->create_init(group_id);
    REQUIRE(std::holds_alternative<create_init::Created>(init_resp5));
  }

  SUBCASE("Commit Conflict")
  {
    // Create the group
    const auto create_init_resp = epoch_server->create_init(group_id);
    REQUIRE(std::holds_alternative<create_init::OK>(create_init_resp));

    const auto create_ok = std::get<create_init::OK>(create_init_resp);
    const auto create_complete_resp =
      epoch_server->create_complete(group_id, create_ok.transaction_id);
    REQUIRE(std::holds_alternative<create_complete::OK>(create_complete_resp));

    // Start a commit
    const auto init_resp = epoch_server->commit_init(group_id, 0);
    REQUIRE(std::holds_alternative<commit_init::OK>(init_resp));

    // Attempting to start again should fail
    const auto init_resp2 = epoch_server->commit_init(group_id, 0);
    REQUIRE(std::holds_alternative<commit_init::Conflict>(init_resp2));

    // Wait until the timer expires
    const auto conflict = std::get<commit_init::Conflict>(init_resp2);
    std::this_thread::sleep_until(conflict.retry_after);

    // Attempting to complete the original transaction should now fail
    const auto ok = std::get<commit_init::OK>(init_resp);
    const auto complete_resp3 =
      epoch_server->commit_complete(group_id, 0, ok.transaction_id);
    REQUIRE(std::holds_alternative<commit_complete::InvalidTransaction>(
      complete_resp3));

    // Attempting to start again should succeed
    const auto init_resp3 = epoch_server->commit_init(group_id, 0);
    REQUIRE(std::holds_alternative<commit_init::OK>(init_resp3));

    const auto ok2 = std::get<commit_init::OK>(init_resp3);
    const auto complete_resp4 =
      epoch_server->commit_complete(group_id, 0, ok2.transaction_id);
    REQUIRE(std::holds_alternative<commit_complete::OK>(complete_resp4));

    // Attempting to commit with a too-old or too-new epoch should fail
    const auto init_resp4 = epoch_server->commit_init(group_id, 0);
    REQUIRE(std::holds_alternative<commit_init::InvalidEpoch>(init_resp4));

    const auto init_resp5 = epoch_server->commit_init(group_id, 2);
    REQUIRE(std::holds_alternative<commit_init::InvalidEpoch>(init_resp5));

    // Commit with the correct new epoch should now succeed
    const auto init_resp6 = epoch_server->commit_init(group_id, 1);
    REQUIRE(std::holds_alternative<commit_init::OK>(init_resp6));

    const auto ok3 = std::get<commit_init::OK>(init_resp6);
    const auto complete_resp5 =
      epoch_server->commit_complete(group_id, 1, ok3.transaction_id);
    REQUIRE(std::holds_alternative<commit_complete::OK>(complete_resp5));
  }
}
