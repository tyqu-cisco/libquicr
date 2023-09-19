#pragma once
#include <cantina/logger.h>
#include <quicr/quicr_client_delegate.h>

#include <future>

class MLSClient;

class SubDelegate : public quicr::SubscriberDelegate
{
public:
  SubDelegate(MLSClient& mls_client_in, cantina::LoggerPointer logger_in, std::promise<bool> on_response_in);

  void onSubscribeResponse(const quicr::Namespace& quicr_namespace,
                           const quicr::SubscribeResult& result) override;

  void onSubscriptionEnded(
    const quicr::Namespace& quicr_namespace,
    const quicr::SubscribeResult::SubscribeStatus& reason) override;

  void onSubscribedObject(const quicr::Name& quicr_name,
                          uint8_t priority,
                          uint16_t expiry_age_ms,
                          bool use_reliable_transport,
                          quicr::bytes&& data) override;

  void onSubscribedObjectFragment(const quicr::Name& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  bool use_reliable_transport,
                                  const uint64_t& offset,
                                  bool is_last_fragment,
                                  quicr::bytes&& data) override;

private:
  cantina::LoggerPointer logger;
  MLSClient& mls_client;
  std::optional<std::promise<bool>> on_response;
};
