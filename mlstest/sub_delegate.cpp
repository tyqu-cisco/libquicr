#include "sub_delegate.h"
#include "mls_client.h"
#include <sstream>

SubDelegate::SubDelegate(cantina::LoggerPointer logger_in,
                         channel::Sender<QuicrObject>&& queue_in)
  : logger(std::move(logger_in))
  , queue(std::move(queue_in))
{
}

bool SubDelegate::await_response() const {
  response_latch.wait();
  return successfully_connected;
}

void
SubDelegate::onSubscribeResponse(const quicr::Namespace& quicr_namespace,
                                 const quicr::SubscribeResult& result)
{
  logger->info << "onSubscriptionResponse: ns: " << quicr_namespace
               << " status: " << static_cast<int>(result.status) << std::flush;

  successfully_connected = result.status ==
                           quicr::SubscribeResult::SubscribeStatus::Ok;
  response_latch.count_down();
}

void
SubDelegate::onSubscriptionEnded(
  const quicr::Namespace& quicr_namespace,
  const quicr::SubscribeResult::SubscribeStatus& reason)

{
  logger->info << "onSubscriptionEnded: ns: " << quicr_namespace
               << " reason: " << static_cast<int>(reason) << std::flush;
}

void
SubDelegate::onSubscribedObject(const quicr::Name& quicr_name,
                                uint8_t /* priority */,
                                uint16_t /* expiry_age_ms */,
                                bool /* use_reliable_transport */,
                                quicr::bytes&& data)
{
  logger->info << "recv object: name: " << quicr_name
               << " data sz: " << data.size();

  if (!data.empty()) {
    logger->info << " data: " << data.data();
  } else {
    logger->info << " (no data)";
  }

  logger->info << std::flush;
  queue.send({ quicr_name, std::move(data) });
}

void
SubDelegate::onSubscribedObjectFragment(const quicr::Name& quicr_name,
                                        uint8_t /* priority */,
                                        uint16_t /* expiry_age_ms */,
                                        bool /* use_reliable_transport */,
                                        const uint64_t& /* offset */,
                                        bool /* is_last_fragment */,
                                        quicr::bytes&& /* data */)
{
  logger->info << "Ignoring object fragment received for " << quicr_name
               << std::flush;
}
