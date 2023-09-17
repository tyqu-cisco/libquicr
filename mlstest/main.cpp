#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "mls_client.h"
#include "namespace_config.h"

#include <thread>

TEST_CASE("Two person test using quicr and mls")
{
  const auto logger = std::make_shared<cantina::Logger>(true);
  const auto ns_config = NamespaceConfig::create_default();

  const auto* relay_var = getenv("MLS_RELAY");
  const auto* port_var = getenv("MLS_PORT");

  const auto hostname = std::string(relay_var ? relay_var : "127.0.0.1");
  const auto port = uint16_t(port_var ? atoi(port_var) : 1234);
  const auto relay = quicr::RelayInfo{
    .hostname = hostname,
    .port = port,
    .proto = quicr::RelayInfo::Protocol::QUIC,
  };

  // Initialize two users
  const auto creator_config = MLSClient::Config{
    .user_id = "FFF000",
    .logger = logger,
    .is_creator = true,
    .relay = relay,
  };
  auto creator = MLSClient{ creator_config };

  const auto joiner_config = MLSClient::Config{
    .user_id = "FFF001",
    .logger = logger,
    .is_creator = false,
    .relay = relay,
  };
  auto joiner = MLSClient{ joiner_config };

  // Subscribe to the relevant namespaces
  const auto namespaces = std::vector<quicr::Namespace>{
    ns_config.key_package,
    ns_config.welcome,
    ns_config.commit,
  };
  for (const auto& ns : namespaces) {
    if (ns != ns_config.welcome) {
      creator.subscribe(ns);
      logger->Log("Creator, Subscribing  to namespace " + std::string(ns));
    }
    joiner.subscribe(ns);
    logger->Log("Subscribing to namespace " + std::string(ns));
  }

  // Joiner publishes KeyPackage
  auto name = ns_config.key_package.name();
  logger->Log("Publishing to " + std::string(name));
  joiner.join(name);

  std::this_thread::sleep_for(std::chrono::seconds(10));
  logger->Log("Sleeping for 10 seconds for mls handshake to complete");

  CHECK_EQ(creator.session().get_state(), joiner.session().get_state());

  std::this_thread::sleep_for(std::chrono::seconds(5));
  logger->Log("Sleeping for 5 seconds before unsubscribing");
}
