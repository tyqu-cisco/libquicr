#include "delivery.h"

#include <quicr/quicr_client_delegate.h>

namespace tls = mls::tls;

namespace delivery {

Service::Service(size_t capacity)
  : inbound_messages(capacity)
{}

// Encoding / decoding for Quicr transport
enum struct MessageType : uint8_t {
  invalid = 0,
  join_request = 1,
  welcome = 2,
  commit = 3,
  leave_request = 4,
};

struct QuicrMessage {
  Message message;

  TLS_SERIALIZABLE(message)
  TLS_TRAITS(tls::variant<MessageType>)
};

static bytes encode(const Message& message) {
  return tls::marshal(QuicrMessage{ message });
}

static Message decode(const bytes& data) {
  return tls::get<QuicrMessage>(data).message;
}

// QuicrService::SubDelegate
struct QuicrService::SubDelegate : public quicr::SubscriberDelegate
{
public:
  SubDelegate(cantina::LoggerPointer logger_in,
              NamespaceConfig namespaces_in,
              channel::Sender<Message> queue_in)
    : logger(logger_in)
    , namespaces(namespaces_in)
    , queue(queue_in)
  {}

  bool await_response() const {
    response_latch.wait();
    return successfully_connected;
  }

  void onSubscribeResponse(const quicr::Namespace& quicr_namespace,
                           const quicr::SubscribeResult& result) override
  {
    logger->info << "onSubscriptionResponse: ns: " << quicr_namespace
                 << " status: " << static_cast<int>(result.status) << std::flush;

    successfully_connected = result.status ==
                             quicr::SubscribeResult::SubscribeStatus::Ok;
    response_latch.count_down();
  }

  void onSubscriptionEnded(
    const quicr::Namespace& quicr_namespace,
    const quicr::SubscribeResult::SubscribeStatus& reason) override
  {
    logger->info << "onSubscriptionEnded: ns: " << quicr_namespace
                 << " reason: " << static_cast<int>(reason) << std::flush;
  }

  void onSubscribedObject(const quicr::Name& quicr_name,
                          uint8_t /* priority */,
                          uint16_t /* expiry_age_ms */,
                          bool /* use_reliable_transport */,
                          quicr::bytes&& data) override {
    logger->info << "recv object: name: " << quicr_name
                 << " data sz: " << data.size();

    if (!data.empty()) {
      logger->info << " data: " << data.data();
    } else {
      logger->info << " (no data)";
    }

    logger->info << std::flush;
    queue.send(decode(data));
  }

  void onSubscribedObjectFragment(const quicr::Name& quicr_name,
                          uint8_t /* priority */,
                          uint16_t /* expiry_age_ms */,
                          bool /* use_reliable_transport */,
                                  const uint64_t& /* offset */,
                                  bool /* is_last_fragment */,
                                  quicr::bytes&& /* data */) override {
    logger->info << "Ignoring object fragment received for " << quicr_name
                 << std::flush;
  }

private:
  cantina::LoggerPointer logger;
  NamespaceConfig namespaces;
  channel::Sender<Message> queue;
  std::latch response_latch{ 1 };
  std::atomic_bool successfully_connected = false;
};

// QuicrService::PubDelegate
struct QuicrService::PubDelegate : public quicr::PublisherDelegate
{
public:
  PubDelegate(cantina::LoggerPointer logger_in)
    : logger(std::move(logger_in))
  {}

  bool await_response() const {
    response_latch.wait();
    return successfully_connected;
  }

  void onPublishIntentResponse(
    const quicr::Namespace& quicr_namespace,
    const quicr::PublishIntentResult& result) override
  {
    std::stringstream log_msg;
    log_msg << "onPublishIntentResponse: name: " << quicr_namespace
            << " status: " << static_cast<int>(result.status);

    logger->Log(log_msg.str());

    successfully_connected = result.status == quicr::messages::Response::Ok;
    response_latch.count_down();
  }

private:
  cantina::LoggerPointer logger;
  std::latch response_latch{ 1 };
  std::atomic_bool successfully_connected = false;
};

// QuicrService
QuicrService::QuicrService(size_t queue_capacity,
                           cantina::LoggerPointer logger_in,
                           std::shared_ptr<quicr::Client> client_in,
                           quicr::Namespace welcome_ns_in,
                           quicr::Namespace group_ns_in,
                           uint32_t user_id_in)
  : Service(queue_capacity)
  , logger(logger_in)
  , client(std::move(client_in))
  , namespaces(welcome_ns_in, group_ns_in, user_id_in)
{}

bool
QuicrService::connect(bool as_creator)
{
  // XXX(richbarn) These subscriptions / publishes are done serially; we await a
  // response for each one before doing the next.  They could be done in
  // parallel by having subscribe/publish_intent return std::future<bool> and
  // awaiting all of these futures together.
  return client->connect()
    // TODO(richbarn) Simplify the set of namespaces
    && (as_creator || subscribe(namespaces.welcome_sub()))
    && subscribe(namespaces.group_sub())
    // Announce intent to publish on this user's namespaces
    && publish_intent(namespaces.welcome_pub())
    && publish_intent(namespaces.group_pub());
}

void
QuicrService::disconnect()
{
  client->disconnect();
}

void
QuicrService::join_request(mls::KeyPackage key_package)
{
  const auto name = namespaces.for_group();
  const auto message = JoinRequest{ std::move(key_package) };
  publish(name, encode(message));
}

void QuicrService::welcome(mls::Welcome welcome)
{
  const auto name = namespaces.for_welcome();
  const auto message = Welcome{ std::move(welcome) };
  publish(name, encode(message));
}

void QuicrService::commit(mls::MLSMessage commit)
{
  const auto name = namespaces.for_group();
  const auto message = Commit{ std::move(commit) };
  publish(name, encode(message));
}

void QuicrService::leave_request(mls::MLSMessage proposal)
{
  const auto name = namespaces.for_group();
  const auto message = LeaveRequest{ std::move(proposal) };
  publish(name, encode(message));
}

bool
QuicrService::subscribe(quicr::Namespace ns)
{
  if (sub_delegates.count(ns)) {
    return true;
  }

  const auto delegate =
    std::make_shared<SubDelegate>(logger, namespaces, make_sender());

  logger->Log("Subscribe to " + std::string(ns));
  quicr::bytes empty;
  client->subscribe(delegate,
                    ns,
                    quicr::SubscribeIntent::immediate,
                    "bogus_origin_url",
                    false,
                    "bogus_auth_token",
                    std::move(empty));

  const auto success = delegate->await_response();
  if (success) {
    sub_delegates.insert_or_assign(ns, delegate);
  }

  return success;
}

bool
QuicrService::publish_intent(quicr::Namespace ns)
{
  logger->Log("Publish Intent for namespace: " + std::string(ns));
  const auto delegate = std::make_shared<PubDelegate>(logger);
  client->publishIntent(delegate, ns, {}, {}, {});
  return delegate->await_response();
}

void
QuicrService::publish(const quicr::Name& name, bytes&& data)
{
  logger->Log("Publish, name=" + std::string(name) +
              " size=" + std::to_string(data.size()));
  client->publishNamedObject(name, 0, default_ttl_ms, false, std::move(data));
}

} // namespace delivery

namespace mls::tls {
TLS_VARIANT_MAP(delivery::MessageType, delivery::JoinRequest, join_request)
TLS_VARIANT_MAP(delivery::MessageType, delivery::Welcome, welcome)
TLS_VARIANT_MAP(delivery::MessageType, delivery::Commit, commit)
TLS_VARIANT_MAP(delivery::MessageType, delivery::LeaveRequest, leave_request)
} // namespace mls::tls
