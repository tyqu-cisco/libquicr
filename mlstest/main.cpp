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

  static constexpr auto user_names = std::array<const char*, 3>{
    "Alice",
    "Bob",
    "Charlie",
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

TEST_CASE_FIXTURE(MLSTest, "Two person test")
{
  group_id = 0x32706172747921;

  // Initialize two users
  auto creator = MLSClient{ next_config() };
  auto joiner = MLSClient{ next_config() };

  // Connect the two clients
  REQUIRE(creator.connect(true));
  REQUIRE(joiner.connect(false));

  // Joiner publishes KeyPackage
  REQUIRE(joiner.join());
  REQUIRE(joiner.joined());

  // Check that both are in the same state
  const auto creator_epoch = creator.next_epoch();
  const auto joiner_epoch = joiner.next_epoch();
  REQUIRE(creator_epoch == joiner_epoch);
}

TEST_CASE_FIXTURE(MLSTest, "Three person test")
{
  group_id = 0x33706172747921;

  // Initialize two users
  auto creator = MLSClient{ next_config() };
  auto joiner1 = MLSClient{ next_config() };
  auto joiner2 = MLSClient{ next_config() };

  // Connect the two clients
  REQUIRE(creator.connect(true));
  REQUIRE(joiner1.connect(false));
  REQUIRE(joiner2.connect(false));

  // Join the first user
  REQUIRE(joiner1.join());
  REQUIRE(joiner1.joined());

  const auto creator_epoch_1 = creator.next_epoch();
  const auto joiner1_epoch_1 = joiner1.next_epoch();
  REQUIRE(creator_epoch_1 == joiner1_epoch_1);

  // Join the second user
  REQUIRE(joiner2.join());
  REQUIRE(joiner2.joined());

  const auto creator_epoch_2 = creator.next_epoch();
  const auto joiner1_epoch_2 = joiner1.next_epoch();
  const auto joiner2_epoch_2 = joiner2.next_epoch();
  REQUIRE(creator_epoch_2 == joiner1_epoch_2);
  REQUIRE(creator_epoch_2 == joiner2_epoch_2);
}
