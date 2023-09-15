#pragma once

#include <map>
#include <memory>

#include "logger.h"
#include "mls_session.h"
#include "sub_delegate.h"
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>

class QuicrClientHelper
{
public:
  QuicrClientHelper(const std::string& user, Logger& logger, bool is_creator);
  void subscribe(quicr::Namespace nspace, Logger& logger);
  void unsubscribe(quicr::Namespace nspace);
  void publishJoin(quicr::Name& name);
  void publishData(quicr::Namespace& nspace, bytes&& data);
  // Subscriber Delagate Operations

  // proxy handlers for quicr messages
  void handle(const quicr::Name& name, quicr::bytes&& data);
  MLSSession& getSession() const;
  bool isUserCreator();

private:
  // helper to create MLS State and User wrapper.
  std::unique_ptr<MLSSession> setupMLSSession(const std::string& user,
                                              const std::string& group,
                                              bool is_creator);

  mls::CipherSuite suite{ mls::CipherSuite::ID::P256_AES128GCM_SHA256_P256 };
  quicr::QuicRClient* client;
  bool is_user_creator;
  std::string user;
  std::string group;
  Logger& logger;
  std::map<quicr::Namespace, std::shared_ptr<SubDelegate>> sub_delegates{};
  std::map<std::string, MLSInitInfo> user_info_map{};
  std::unique_ptr<MLSSession> session = nullptr;
};
