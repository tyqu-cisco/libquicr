#pragma once

#include "mls_session.h"
#include "namespace_config.h"
#include "sub_delegate.h"

#include <cantina/logger.h>
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>

#include <map>
#include <memory>

class MLSClient
{
public:
  struct Config
  {
    const std::string user_id;
    cantina::LoggerPointer logger;
    bool is_creator;
    quicr::RelayInfo relay;
  };

  MLSClient(const Config& config);

  // Connect to the server and make subscriptions
  bool connect(std::string user_id, bool as_creator);

  // Control publications and subscriptions
  void subscribe(quicr::Namespace nspace);
  void unsubscribe(quicr::Namespace nspace);
  void publish(quicr::Namespace& nspace, bytes&& data);

  // MLS operations
  void join(quicr::Name& name);
  void handle(const quicr::Name& name, quicr::bytes&& data);

  // Access internal state
  bool joined() const;
  const MLSSession& session() const;

private:
  mls::CipherSuite suite{ mls::CipherSuite::ID::P256_AES128GCM_SHA256_P256 };

  cantina::LoggerPointer logger;

  NamespaceConfig namespaces;
  std::unique_ptr<quicr::QuicRClient> client;
  std::map<quicr::Namespace, std::shared_ptr<SubDelegate>> sub_delegates{};

  std::variant<MLSInitInfo, MLSSession> mls_session;
};
