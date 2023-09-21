#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "mls_client.h"

#include <thread>
using namespace std::chrono_literals;

class MLSTest
{
public:
  MLSTest()
    : logger(std::make_shared<cantina::Logger>(true))
  {
    const auto* relay_var = getenv("MLS_RELAY");
    const auto* port_var = getenv("MLS_PORT");

    const auto hostname = std::string(relay_var ? relay_var : "127.0.0.1");
    const auto port = uint16_t(port_var ? atoi(port_var) : 1234);
    relay = quicr::RelayInfo{
      .hostname = hostname,
      .port = port,
      .proto = quicr::RelayInfo::Protocol::QUIC,
    };
  }

protected:
  const cantina::LoggerPointer logger;
  quicr::RelayInfo relay;

  // The group_id needs to be different for different test cases.  Otherwise the
  // relay will reject the publish intents from different clients for the same
  // namespace.
  uint64_t group_id = 0;
  uint32_t next_user_id = 0x00000000;

  static constexpr auto user_names = std::array<const char*, 5>{
    "Alice",
    "Bob",
    "Charlie",
    "Diana",
    "Ellen",
  };

  MLSClient::Config next_config() {
    const auto* user_name = user_names.at(next_user_id);
    const auto user_logger = std::make_shared<cantina::Logger>(user_name, logger, true);
    const auto config = MLSClient::Config{
      .group_id = group_id,
      .user_id = next_user_id,
      .logger = user_logger,
      .relay = relay,
    };

    next_user_id += 1;
    return config;
  }
};

TEST_CASE_FIXTURE(MLSTest, "Set up two-person MLS")
{
  group_id = 0x32706172747921;

  // Initialize and connect two users
  auto creator = MLSClient{ next_config() };
  REQUIRE(creator.connect(true));

  auto joiner = MLSClient{ next_config() };
  REQUIRE(joiner.connect(false));

  // Joiner publishes KeyPackage
  REQUIRE(joiner.join());
  REQUIRE(joiner.joined());

  // Check that both are in the same state
  const auto creator_epoch = creator.next_epoch();
  const auto joiner_epoch = joiner.next_epoch();
  REQUIRE(creator_epoch == joiner_epoch);
}

TEST_CASE_FIXTURE(MLSTest, "Set up group MLS")
{
  group_id = 0x33706172747921;
  const auto group_size = user_names.size();

  // Initialize and connect the creator
  auto creator = MLSClient{ next_config() };
  creator.connect(true);

  // For each remaining client...
  // (The shared_ptr is necessary to store an MLSClient in a vector, since
  // MLSClient itself doesn't meet the requirements for MoveInsertable.)
  auto joiners = std::vector<std::shared_ptr<MLSClient>>{};
  for (size_t i = 1; i < group_size; i++) {
    // Initialize
    auto joiner = std::make_shared<MLSClient>(next_config());
    joiners.push_back(joiner);

    // Join the group
    REQUIRE(joiner->connect(false));
    REQUIRE(joiner->join());
    REQUIRE(joiner->joined());

    // Verify that all clients are in the same state
    const auto creator_epoch = creator.next_epoch();
    for (auto& joiner : joiners) {
      REQUIRE(creator_epoch == joiner->next_epoch());
    }
  }
}
