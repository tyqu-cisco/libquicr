#pragma once

#include "channel.h"

#include <cantina/logger.h>
#include <quicr/quicr_client_delegate.h>

#include <future>
#include <latch>

class MLSClient;

struct QuicrObject
{
  quicr::Name name;
  quicr::bytes data;
};

class SubDelegate : public quicr::SubscriberDelegate
{
public:
  SubDelegate(cantina::LoggerPointer logger_in,
              channel::Sender<QuicrObject>&& queue_in);

  bool await_response() const;

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
  channel::Sender<QuicrObject> queue;
  std::latch response_latch{ 1 };
  std::atomic_bool successfully_connected = false;
};
