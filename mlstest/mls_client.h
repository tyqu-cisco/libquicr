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
  // Logging
  cantina::LoggerPointer logger;

  // Pub/Sub operations
  uint64_t group_id;
  uint32_t user_id;
  NamespaceConfig namespaces;

  std::unique_ptr<quicr::Client> client;
  std::map<quicr::Namespace, std::shared_ptr<SubDelegate>> sub_delegates{};

  bool subscribe(quicr::Namespace nspace);
  bool publish_intent(quicr::Namespace nspace);
  void publish(const quicr::Name& name, bytes&& data);

  // MLS operations
  const mls::CipherSuite suite{
    mls::CipherSuite::ID::P256_AES128GCM_SHA256_P256
  };

  std::optional<std::promise<bool>> join_promise;
  std::variant<MLSInitInfo, MLSSession> mls_session;

  std::map<uint64_t, std::map<uint32_t, size_t>> commit_votes;
  std::map<uint64_t, std::map<uint32_t, bytes>> commit_cache;
  AsyncQueue<Epoch> epochs;

  bool should_commit(size_t n_adds,
                     const std::vector<ParsedLeaveRequest>& removed) const;

  // One lock for the whole object; one stop signal for all threads
  std::recursive_mutex self_mutex;
  std::unique_lock<std::recursive_mutex> lock()
  {
    return std::unique_lock{ self_mutex };
  }

  std::atomic_bool stop_threads = false;

  // Handler thread, including out-of-order message handling
  struct PendingWelcome
  {
    bytes commit;
    bytes welcome;
    std::vector<quicr::Name> welcome_names;
  };

  static constexpr auto inbound_object_timeout = std::chrono::milliseconds(100);
  std::shared_ptr<AsyncQueue<QuicrObject>> inbound_objects;
  std::vector<QuicrObject> future_epoch_objects;
  std::optional<PendingWelcome> pending_welcome;
  std::optional<std::thread> handler_thread;

  void handle(QuicrObject&& obj);
  void advance_if_quorum();

  // Commit thread
  static constexpr auto commit_interval = std::chrono::milliseconds(250);
  std::vector<ParsedJoinRequest> joins_to_commit;
  std::vector<ParsedLeaveRequest> leaves_to_commit;
  std::optional<mls::LeafNode> old_leaf_node_to_commit;

  std::optional<std::thread> commit_thread;
  void make_commit();
};
