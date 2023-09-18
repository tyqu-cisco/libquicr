#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "mls_client.h"
#include "namespace_config.h"

#include <thread>

TEST_CASE("Namespace Config")
{
  using quicr::Namespace;
  const auto namespaces = NamespaceConfig(0x01020304050607);

  REQUIRE(namespaces.key_package_sub() ==
          Namespace("0x01020304050607010000000000000000/64"));
  REQUIRE(namespaces.welcome_sub() ==
          Namespace("0x01020304050607020000000000000000/64"));
  REQUIRE(namespaces.commit_sub() ==
          Namespace("0x01020304050607030000000000000000/64"));

  const auto user_id = uint32_t(0x0a0b0c0d);
  REQUIRE(namespaces.key_package_pub(user_id) ==
          Namespace("0x01020304050607010a0b0c0d00000000/96"));
  REQUIRE(namespaces.welcome_pub(user_id) ==
          Namespace("0x01020304050607020a0b0c0d00000000/96"));
  REQUIRE(namespaces.commit_pub(user_id) ==
          Namespace("0x01020304050607030a0b0c0d00000000/96"));

  const auto third_value = uint32_t(0xf0f1f2f3);
  REQUIRE(namespaces.for_key_package(user_id, third_value) ==
          0x01020304050607010a0b0c0df0f1f2f3_name);
  REQUIRE(namespaces.for_welcome(user_id, third_value) ==
          0x01020304050607020a0b0c0df0f1f2f3_name);
  REQUIRE(namespaces.for_commit(user_id, third_value) ==
          0x01020304050607030a0b0c0df0f1f2f3_name);
}

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

  // Connect the two clients
  creator.connect(true);
  joiner.connect(false);

  // Joiner publishes KeyPackage
  joiner.join();

  std::this_thread::sleep_for(std::chrono::seconds(10));
  logger->Log("Sleeping for 10 seconds for mls handshake to complete");

  CHECK_EQ(creator.session().get_state(), joiner.session().get_state());

  std::this_thread::sleep_for(std::chrono::seconds(5));
  logger->Log("Sleeping for 5 seconds before unsubscribing");
}
