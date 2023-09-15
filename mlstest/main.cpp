#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "logger.h"
#include "namespace_config.h"
#include "quicr_client_helper.h"

#include <thread>

TEST_CASE("Two person test using quicr and mls")
{
  auto logger = Logger{};
  const auto nspace_config = NamespaceConfig::create_default();
  auto creator = QuicrClientHelper{ "FFFOOO", logger, true };
  auto joiner = QuicrClientHelper{ "FFFOO1", logger, false };

  // subscribe for all
  const auto namespaces = std::vector<quicr::Namespace>{
    nspace_config.key_package,
    nspace_config.welcome,
    nspace_config.commit,
  };
  for (const auto& ns : namespaces) {
    if (ns != nspace_config.welcome) {
      creator.subscribe(ns, logger);
      logger.log(qtransport::LogLevel::info,
                 "Creator, Subscribing  to namespace " + ns.to_hex());
    }
    joiner.subscribe(ns, logger);
    logger.log(qtransport::LogLevel::info,
               "Subscribing to namespace " + ns.to_hex());
  }

  // participant publish keypackage
  if (!joiner.isUserCreator()) {
    auto name = nspace_config.key_package.name();
    logger.log(qtransport::LogLevel::info, "Publishing to " + name.to_hex());
    joiner.publishJoin(name);
  }

  std::this_thread::sleep_for(std::chrono::seconds(10));
  logger.log(qtransport::LogLevel::info,
             "Sleeping for 10 seconds for mls handshake to complete");

  CHECK_EQ(creator.getSession().get_state(), joiner.getSession().get_state());

  std::this_thread::sleep_for(std::chrono::seconds(5));
  logger.log(qtransport::LogLevel::info,
             "Sleeping for 5 seconds before unsubscribing");
}
