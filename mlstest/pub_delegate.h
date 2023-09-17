#pragma once
#include "logger.h"
#include <quicr/quicr_client_delegate.h>

class PubDelegate : public quicr::PublisherDelegate
{
public:
  PubDelegate(Logger& logger_in);

  void onPublishIntentResponse(
    const quicr::Namespace& quicr_namespace,
    const quicr::PublishIntentResult& result) override;

private:
  Logger& logger;
};
