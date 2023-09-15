
#include "quicr_client_secure.h"
#include <doctest/doctest.h>

TEST_CASE("Test mlstest")
{
  CHECK_EQ(1, 1);
}

TEST_CASE("Two person test using quicr and mls")
{
  auto user_string = std::string("FFFOOO");
  auto name = quicr::Name(std::string("FFF000"));
  common_utils utils;
  utils.log_msg << "Name = " << name.to_hex();

  // subscribe for all
  const auto namespaces = std::vector<quicr::Namespace>{
    utils.nspace_config.key_package,
    utils.nspace_config.welcome,
    utils.nspace_config.commit,
  };
  for (const auto& ns : namespaces) {
    if (ns != utils.nspace_config.welcome) {
      utils.creator.subscribe(ns, utils.logger);
      utils.logger.log(qtransport::LogLevel::info,
                       "Creator, Subscribing  to namespace " + ns.to_hex());
    }
    utils.participants[0].subscribe(ns, utils.logger);
    utils.logger.log(qtransport::LogLevel::info,
                     "Subscribing to namespace " + ns.to_hex());
  }

  // participant publish keypackage
  if (!utils.participants[0].isUserCreator()) {
    auto name = utils.nspace_config.key_package.name();
    utils.logger.log(qtransport::LogLevel::info,
                     "Publishing to " + name.to_hex());
    utils.participants[0].publishJoin(name);
  }

  std::this_thread::sleep_for(std::chrono::seconds(10));
  utils.logger.log(qtransport::LogLevel::info,
                   "Sleeping for 10 seconds for mls handshake to complete");

  CHECK_EQ(utils.creator.getSession().get_state(),
           utils.participants[0].getSession().get_state());

  std::this_thread::sleep_for(std::chrono::seconds(5));
  utils.logger.log(qtransport::LogLevel::info,
                   "Sleeping for 5 seconds before unsubscribing");
}
