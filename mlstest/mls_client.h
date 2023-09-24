#pragma once

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
#include <queue>
#include <thread>

template<typename T>
struct AsyncQueue
{
  void push(const T& val)
  {
    std::unique_lock<std::mutex> lock(mutex);
    queue.push(val);
    lock.unlock();
    nonempty.notify_all();
  }

  T pop()
  {
    std::unique_lock<std::mutex> lock(mutex);
    nonempty.wait(lock, [&] { return !queue.empty(); });
    const auto val = queue.front();
    queue.pop();
    return val;
  }

  std::mutex mutex;
  std::condition_variable nonempty;
  std::queue<T> queue;
};

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

  // Connect to the server and make subscriptions
  bool connect(bool as_creator);

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
  AsyncQueue<Epoch> epochs;
  bool should_commit() const;

  std::unique_ptr<quicr::QuicRClient> client;
  std::map<quicr::Namespace, std::shared_ptr<SubDelegate>> sub_delegates{};

  bool subscribe(quicr::Namespace nspace);
  bool publish_intent(quicr::Namespace nspace);
  void publish(const quicr::Name& name, bytes&& data);
  void handle(const quicr::Name& name, quicr::bytes&& data);

  friend class SubDelegate;
};
