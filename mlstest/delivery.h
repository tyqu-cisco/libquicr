#pragma once

#include "channel.h"
#include "namespace_config.h"

#include <mls/messages.h>
#include <quicr/quicr_client.h>

#include <future>
#include <latch>

namespace delivery {

using mls::bytes_ns::bytes;

using UserID = uint32_t;
using JoinID = uint32_t;
using EpochID = uint64_t;

struct JoinRequest {
  mls::KeyPackage key_package;

  TLS_SERIALIZABLE(key_package)
};

struct Welcome {
  mls::Welcome welcome;

  TLS_SERIALIZABLE(welcome)
};

struct Commit {
  mls::MLSMessage commit;

  TLS_SERIALIZABLE(commit)
};

struct LeaveRequest {
  mls::MLSMessage proposal;

  TLS_SERIALIZABLE(proposal)
};

using Message = std::variant<JoinRequest, Welcome, Commit, LeaveRequest>;

struct Service {
  Service(size_t capacity);

  virtual ~Service() = default;

  // Connect to the service
  virtual bool connect(bool as_creator) = 0;

  // Disconnect from the service
  virtual void disconnect() = 0;

  // Publish a JoinRequest containing the specified KeyPackage.
  virtual void join_request(mls::KeyPackage key_package) = 0;

  // Respond to a JoinRequest with a Welcome message
  virtual void welcome(mls::Welcome welcome) = 0;

  // Broadcast a Commit message to the group
  virtual void commit(mls::MLSMessage commit) = 0;

  // Broadcast a LeaveRequest to the group
  virtual void leave_request(mls::MLSMessage proposal) = 0;

  // Read incoming messages
  channel::Receiver<Message> inbound_messages;

protected:
  channel::Sender<Message> make_sender() {
    return inbound_messages.make_sender();
  }
};

struct QuicrService : Service {
  QuicrService(size_t queue_capacity,
               cantina::LoggerPointer logger_in,
               std::shared_ptr<quicr::Client> client,
               quicr::Namespace welcome_ns_in,
               quicr::Namespace group_ns_in,
               uint32_t user_id);

  bool connect(bool as_creator) override;
  void disconnect() override;

  void join_request(mls::KeyPackage key_package) override;
  void welcome(mls::Welcome welcome) override;
  void commit(mls::MLSMessage commit) override;
  void leave_request(mls::MLSMessage proposal) override;

private:
  static const uint16_t default_ttl_ms = 1000;

  cantina::LoggerPointer logger;
  std::shared_ptr<quicr::Client> client;
  NamespaceConfig namespaces;

  bool subscribe(quicr::Namespace nspace);
  bool publish_intent(quicr::Namespace nspace);
  void publish(const quicr::Name& name, bytes&& data);

  struct SubDelegate;
  struct PubDelegate;
  std::map<quicr::Namespace, std::shared_ptr<SubDelegate>> sub_delegates{};
};

} // namespace delivery
