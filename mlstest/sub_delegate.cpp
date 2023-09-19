#include "sub_delegate.h"
#include "mls_client.h"
#include <sstream>

SubDelegate::SubDelegate(MLSClient& mls_client_in,
                         cantina::LoggerPointer logger_in, std::promise<bool> on_response_in)
  : logger(std::move(logger_in))
  , mls_client(mls_client_in)
  , on_response(std::move(on_response_in))
{
}

void
SubDelegate::onSubscribeResponse(const quicr::Namespace& quicr_namespace,
                                 const quicr::SubscribeResult& result)
{
  std::stringstream log_msg;
  log_msg << "onSubscriptionResponse: ns: " << quicr_namespace
          << " status: " << static_cast<int>(result.status);

  logger->Log(log_msg.str());

  if (on_response) {
    on_response->set_value(result.status == quicr::SubscribeResult::SubscribeStatus::Ok);
    on_response.reset();
  }
}

void
SubDelegate::onSubscriptionEnded(
  const quicr::Namespace& quicr_namespace,
  const quicr::SubscribeResult::SubscribeStatus& reason)

{
  std::stringstream log_msg;
  log_msg << "onSubscriptionEnded: ns: " << quicr_namespace
          << " reason: " << static_cast<int>(reason);

  logger->Log(log_msg.str());
}

void
SubDelegate::onSubscribedObject(const quicr::Name& quicr_name,
                                uint8_t /* priority */,
                                uint16_t /* expiry_age_ms */,
                                bool /* use_reliable_transport */,
                                quicr::bytes&& data)
{
  std::stringstream log_msg;
  log_msg << "recv object: name: " << quicr_name << " data sz: " << data.size();

  if (!data.empty()) {
    log_msg << " data: " << data.data();
  } else {
    log_msg << " (no data)";
  }

  logger->Log(log_msg.str());
  mls_client.handle(quicr_name, std::move(data));
}

void
SubDelegate::onSubscribedObjectFragment(const quicr::Name& quicr_name,
                                        uint8_t /* priority */,
                                        uint16_t /* expiry_age_ms */,
                                        bool /* use_reliable_transport */,
                                        const uint64_t& offset,
                                        bool is_last_fragment,
                                        quicr::bytes&& data)
{
  std::stringstream log_msg;
  log_msg << "recv object: name: " << quicr_name << " fragment no: " << offset
          << (is_last_fragment ? "(final)" : "(non-final)")
          << " data sz: " << data.size();

  if (!data.empty()) {
    log_msg << " data: " << data.data();
  } else {
    log_msg << " (no data)";
  }

  logger->Log(log_msg.str());

  // TODO(RLB): Handle fragmented objects.
  // XXX(RLB): Why doean't libquicr handle reassembly??
}
