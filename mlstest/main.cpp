#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "mls_client.h"

TEST_CASE("Two person test using quicr and mls")
{
  const auto logger = std::make_shared<cantina::Logger>(true);

  const auto* relay_var = getenv("MLS_RELAY");
  const auto* port_var = getenv("MLS_PORT");

  const auto hostname = std::string(relay_var ? relay_var : "127.0.0.1");
  const auto port = uint16_t(port_var ? atoi(port_var) : 1234);
  const auto relay = quicr::RelayInfo{
    .hostname = hostname,
    .port = port,
    .proto = quicr::RelayInfo::Protocol::QUIC,
  };

  const auto group_id = uint64_t(0x010203040506);

  // Initialize two users
  const auto creator_config = MLSClient::Config{
    .group_id = group_id,
    .user_id = 0x01,
    .logger = logger,
    .relay = relay,
  };
  auto creator = MLSClient{ creator_config };

  const auto joiner_config = MLSClient::Config{
    .group_id = group_id,
    .user_id = 0x02,
    .logger = logger,
    .relay = relay,
  };
  auto joiner = MLSClient{ joiner_config };

  // Connect the two clients, including brief pauses to let the publishIntent
  // responses come in
  REQUIRE(creator.connect(true));
  REQUIRE(joiner.connect(false));

  // Joiner publishes KeyPackage
  joiner.join();

  CHECK_EQ(creator.session().get_state(), joiner.session().get_state());
}
