#pragma once

#include "async_queue.h"
#include "mls_session.h"
#include "namespace_config.h"
#include "sub_delegate.h"

#include <cantina/logger.h>
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>

#include <condition_variable>
#include <future>
#include <map>
#include <memory>
#include <thread>

class MLSClient
{
public:
  struct Config
  {
    uint64_t group_id;
    uint32_t user_id;
    cantina::LoggerPointer logger;
    quicr::RelayInfo relay;
  };

  explicit MLSClient(const Config& config);
  ~MLSClient();

  // Connect to the server and make subscriptions
  bool connect(bool as_creator);
  void disconnect();

  // MLS operations
  bool join();
  void leave();

  // Access internal state
  bool joined() const;
  const MLSSession& session() const;

  // Access to MLS epochs as they arrive.  This method pops a queue of epochs;
  // if there are no epoch enqueued, it will block until one shows up.
  struct Epoch
  {
    uint64_t epoch;
    size_t member_count;
    bytes epoch_authenticator;

    friend bool operator==(const Epoch& lhs, const Epoch& rhs);
  };
  Epoch next_epoch();

private:
  mls::CipherSuite suite{ mls::CipherSuite::ID::P256_AES128GCM_SHA256_P256 };

  cantina::LoggerPointer logger;

  uint64_t group_id;
  uint32_t user_id;
  NamespaceConfig namespaces;

  struct PendingJoin
  {
    MLSInitInfo init_info;
    std::promise<bool> joined;
  };

  std::optional<std::promise<bool>> join_promise;
  std::variant<MLSInitInfo, MLSSession> mls_session;

  std::map<uint64_t, std::map<uint32_t, size_t>> commit_votes;
  std::map<uint64_t, std::map<uint32_t, bytes>> commit_cache;
  AsyncQueue<Epoch> epochs;

  bool should_commit(size_t n_adds,
                     const std::vector<mls::LeafIndex>& removed) const;

  std::unique_ptr<quicr::Client> client;
  std::map<quicr::Namespace, std::shared_ptr<SubDelegate>> sub_delegates{};

  std::shared_ptr<AsyncQueue<QuicrObject>> inbound_objects;
  std::optional<std::thread> handler_thread;
  std::atomic_bool handler_thread_stop = false;

  bool subscribe(quicr::Namespace nspace);
  bool publish_intent(quicr::Namespace nspace);
  void publish(const quicr::Name& name, bytes&& data);
  void enqueue(QuicrObject&& obj);
  void handle(const quicr::Name& name, quicr::bytes&& data);
  void publish_commit(bytes&& commit_data);
  void advance_if_quorum();

  static constexpr auto inbound_object_timeout = std::chrono::milliseconds(100);

  friend class SubDelegate;
};
