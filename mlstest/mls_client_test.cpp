#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "mls_client.h"
#include "namespace_config.h"

#include <transport/transport.h>

#include <random>
#include <thread>
using namespace std::chrono_literals;

class MLSTest
{
public:
  MLSTest()
    : logger(std::make_shared<cantina::Logger>(true))
    , lock_service(std::make_shared<lock::InMemoryService>(logger))
  {
    // Connect to a relay
    const auto* relay_var = getenv("MLS_RELAY");
    const auto* port_var = getenv("MLS_PORT");

    const auto hostname = std::string(relay_var ? relay_var : "127.0.0.1");
    const auto port = uint16_t(port_var ? atoi(port_var) : 1234);
    relay = quicr::RelayInfo{
      .hostname = hostname,
      .port = port,
      .proto = quicr::RelayInfo::Protocol::QUIC,
    };

    // Assign a random group ID to avoid conflicts
    auto dist = std::uniform_int_distribution<uint64_t>(
      std::numeric_limits<uint64_t>::min(),
      std::numeric_limits<uint64_t>::max());
    auto engine = std::random_device();
    const auto group_id = dist(engine);
    const auto group_id_ns = SubNamespace{}.extend(group_id, 63);
    welcome_ns = group_id_ns.extend(0x00, 8);
    group_ns = group_id_ns.extend(0x01, 8);
  }

protected:
  const cantina::LoggerPointer logger;
  quicr::RelayInfo relay;
  qtransport::TransportConfig tcfg{ .tls_cert_filename = NULL,
                                    .tls_key_filename = NULL };

  uint64_t group_id = 0;
  uint32_t next_endpoint_id = 0x00000000;
  size_t message_queue_capacity = 10;
  quicr::Namespace welcome_ns;
  quicr::Namespace group_ns;

  std::shared_ptr<lock::Service> lock_service;

  static constexpr auto user_names = std::array<const char*, 5>{
    "Alice", "Bob", "Charlie", "Diana", "Ellen",
  };

  MLSClient::Config next_config()
  {
    const auto* user_name = user_names.at(next_endpoint_id);
    const auto user_logger =
      std::make_shared<cantina::Logger>(user_name, logger, true);

    const auto client = std::make_shared<quicr::Client>(relay, tcfg, logger);
    const auto delivery_service =
      std::make_shared<delivery::QuicrService>(message_queue_capacity,
                                               user_logger,
                                               client,
                                               welcome_ns,
                                               group_ns,
                                               next_endpoint_id);

    const auto config = MLSClient::Config{
      .group_id = group_id,
      .endpoint_id = next_endpoint_id,
      .logger = user_logger,
      .lock_service = lock_service,
      .delivery_service = delivery_service,
    };

    next_endpoint_id += 1;
    return config;
  }
};

TEST_CASE_FIXTURE(MLSTest, "Create a two-person group")
{
  // Initialize and connect two users
  auto creator = MLSClient{ next_config() };
  REQUIRE(creator.connect());

  auto joiner = MLSClient{ next_config() };
  REQUIRE(joiner.connect());

  // Joiner publishes KeyPackage
  REQUIRE(joiner.join().get());
  REQUIRE(joiner.joined());

  // Check that both are in the same state
  {
    const auto creator_epoch = creator.next_epoch();
    const auto joiner_epoch = joiner.next_epoch();
    REQUIRE(creator_epoch.epoch == 1);
    REQUIRE(creator_epoch.member_count == 2);
    REQUIRE(creator_epoch == joiner_epoch);
  }

  // Check that the joiner sent a commit
  {
    const auto creator_epoch = creator.next_epoch();
    const auto joiner_epoch = joiner.next_epoch();
    REQUIRE(creator_epoch.epoch == 2);
    REQUIRE(creator_epoch.member_count == 2);
    REQUIRE(creator_epoch == joiner_epoch);
  }
}

TEST_CASE_FIXTURE(MLSTest, "Create a large group")
{
  const auto group_size = user_names.size();

  // Initialize and connect the creator
  auto creator = MLSClient{ next_config() };
  creator.connect();

  // For each remaining client...
  // (The shared_ptr is necessary to store an MLSClient in a vector, since
  // MLSClient itself doesn't meet the requirements for MoveInsertable.)
  auto joiners = std::vector<std::shared_ptr<MLSClient>>{};
  for (size_t i = 1; i < group_size; i++) {
    // Initialize
    auto joiner = std::make_shared<MLSClient>(next_config());
    joiners.push_back(joiner);

    // Join the group
    REQUIRE(joiner->connect());
    REQUIRE(joiner->join().get());
    REQUIRE(joiner->joined());

    // Verify that all clients processed the join
    {
      const auto creator_epoch = creator.next_epoch();
      REQUIRE(creator_epoch.epoch == 2 * i - 1);
      REQUIRE(creator_epoch.member_count == i + 1);
      for (auto& joiner : joiners) {
        REQUIRE(creator_epoch == joiner->next_epoch());
      }
    }

    // Verify that all clients processed the joiner's commit
    {
      const auto creator_epoch = creator.next_epoch();
      REQUIRE(creator_epoch.epoch == 2 * i);
      REQUIRE(creator_epoch.member_count == i + 1);
      for (auto& joiner : joiners) {
        REQUIRE(creator_epoch == joiner->next_epoch());
      }
    }
  }
}

TEST_CASE_FIXTURE(MLSTest, "Create a large group then tear down")
{
  const auto group_size = user_names.size();

  // Initialize and connect the creator
  auto creator = MLSClient{ next_config() };
  creator.connect();

  // Add each remaining client
  // (The shared_ptr is necessary to store an MLSClient in a vector, since
  // MLSClient itself doesn't meet the requirements for MoveInsertable.)
  auto expected_epoch = uint64_t(0);
  auto members = std::deque<std::shared_ptr<MLSClient>>{};
  for (size_t i = 1; i < group_size; i++) {
    // Initialize
    auto joiner = std::make_shared<MLSClient>(next_config());
    members.push_back(joiner);

    // Join the group
    REQUIRE(joiner->connect());
    REQUIRE(joiner->join().get());
    REQUIRE(joiner->joined());

    // Verify that all clients processed the join
    {
      expected_epoch += 1;
      const auto creator_epoch = creator.next_epoch();
      REQUIRE(creator_epoch.epoch == expected_epoch);
      REQUIRE(creator_epoch.member_count == i + 1);
      for (auto& member : members) {
        REQUIRE(creator_epoch == member->next_epoch());
      }
    }

    // Verify that all clients processed the joiner's commit
    {
      expected_epoch += 1;
      const auto creator_epoch = creator.next_epoch();
      REQUIRE(creator_epoch.epoch == expected_epoch);
      REQUIRE(creator_epoch.member_count == i + 1);
      for (auto& member : members) {
        REQUIRE(creator_epoch == member->next_epoch());
      }
    }
  }

  // The creator leaves
  creator.leave();

  // Validate that all members are in the same state
  const auto require_same = [](auto expected_epoch, const auto& members) {
    const auto& reference_member = members.front();
    const auto reference_epoch = reference_member->next_epoch();
    REQUIRE(reference_epoch.epoch == expected_epoch);
    REQUIRE(reference_epoch.member_count == members.size());
    for (auto& member : members) {
      if (member == reference_member) {
        continue;
      }

      REQUIRE(reference_epoch == member->next_epoch());
    }
  };

  expected_epoch += 1;
  require_same(expected_epoch, members);

  // All clients but the creator leave
  while (members.size() > 1) {
    auto leaver = members.front();
    members.pop_front();

    // Leave the group
    leaver->leave();

    // Verify that all clients are in the same state
    expected_epoch += 1;
    require_same(expected_epoch, members);
  }
}

TEST_CASE_FIXTURE(MLSTest, "Create a large group in parallel")
{
  const auto group_size = user_names.size();

  // Initialize and connect the creator
  auto creator = MLSClient{ next_config() };
  creator.connect();

  // For each remaining client...
  // (The shared_ptr is necessary to store an MLSClient in a vector, since
  // MLSClient itself doesn't meet the requirements for MoveInsertable.)
  auto joiners = std::vector<std::shared_ptr<MLSClient>>{};
  auto join_futures = std::vector<std::future<bool>>{};
  for (size_t i = 1; i < group_size; i++) {
    // Initialize
    auto joiner = std::make_shared<MLSClient>(next_config());
    joiners.push_back(joiner);

    // Join the group without waiting
    REQUIRE(joiner->connect());
    join_futures.push_back(joiner->join());
  }

  // Wait until all joiners have joined
  for (auto i = size_t(0); i < joiners.size(); i++) {
    REQUIRE(join_futures.at(i).get());
    REQUIRE(joiners.at(i)->joined());
  }

  // Brief pause to let all the commits quiet down
  std::this_thread::sleep_for(200ms);

  // Verify that all members arrived at the same place
  const auto max_expected_epoch = 2 * joiners.size();
  const auto creator_epoch = creator.latest_epoch();
  REQUIRE(creator_epoch.epoch <= max_expected_epoch);
  REQUIRE(creator_epoch.member_count == joiners.size() + 1);
  for (auto& joiner : joiners) {
    REQUIRE(creator_epoch == joiner->latest_epoch());
  }
}

TEST_CASE_FIXTURE(MLSTest, "Create and tear down a large group in parallel")
{
  const auto group_size = user_names.size();

  // Initialize and connect the creator
  auto creator = MLSClient{ next_config() };
  creator.connect();

  // For each remaining client...
  // (The shared_ptr is necessary to store an MLSClient in a vector, since
  // MLSClient itself doesn't meet the requirements for MoveInsertable.)
  auto joiners = std::vector<std::shared_ptr<MLSClient>>{};
  auto join_futures = std::vector<std::future<bool>>{};
  for (size_t i = 1; i < group_size; i++) {
    // Initialize
    auto joiner = std::make_shared<MLSClient>(next_config());
    joiners.push_back(joiner);

    // Join the group without waiting
    REQUIRE(joiner->connect());
    join_futures.push_back(joiner->join());
  }

  // Wait until all joiners have joined
  for (auto i = size_t(0); i < joiners.size(); i++) {
    REQUIRE(join_futures.at(i).get());
    REQUIRE(joiners.at(i)->joined());
  }

  // Brief pause to let all the commits quiet down
  std::this_thread::sleep_for(200ms);

  // Everyone rushes for the door
  for (auto& joiner : joiners) {
    joiner->leave();
  }

  // Brief pause to let all the commits quiet down
  std::this_thread::sleep_for(500ms);

  // Verify that the creator is now alone
  const auto max_expected_epoch = 3 * joiners.size();
  const auto creator_epoch = creator.latest_epoch();
  REQUIRE(creator_epoch.epoch <= max_expected_epoch);
  REQUIRE(creator_epoch.member_count == 1);
}
