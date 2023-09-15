#pragma once
#include <mutex>
#include <transport/logger.h>

class Logger : public qtransport::LogHandler
{
public:
  void log(qtransport::LogLevel level, const std::string& string) override;

private:
  std::mutex mutex;
};
