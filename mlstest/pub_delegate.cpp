#include "pub_delegate.h"
#include <sstream>

PubDelegate::PubDelegate(Logger& logger_in)
  : logger(logger_in)
{
}

void
PubDelegate::onPublishIntentResponse(const quicr::Namespace& quicr_namespace,
                                     const quicr::PublishIntentResult& result)
{
  std::stringstream log_msg;
  log_msg << "onSubscriptionResponse: name: " << quicr_namespace.to_hex() << "/"
          << quicr_namespace.length()
          << " status: " << static_cast<int>(result.status);

  logger.log(qtransport::LogLevel::info, log_msg.str());
}
