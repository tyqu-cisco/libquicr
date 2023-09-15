#ifndef QUICR_QUICR_CLIENT_SECURE_H
#define QUICR_QUICR_CLIENT_SECURE_H

#include <chrono>
#include <cstring>
#include <iostream>
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>
#include <sstream>
#include <thread>

#include <bytes/bytes.h>
#include <hpke/random.h>
#include <mls/common.h>
#include <mls/state.h>

#include "namespace_config.h"
#include "mls_user_session.h"
#include "pub_delegate.h"
#include "quicr_client_helper.h"
#include "logger.h"
#include <array>

using namespace mls;

struct common_utils
{
  Logger logger;
  std::stringstream log_msg;
  NamespaceConfig nspace_config = NamespaceConfig::create_default();
  QuicrClientHelper creator{ std::string("FFFOOO"), logger, true };
  std::array<QuicrClientHelper, 1> participants = {
    QuicrClientHelper(std::string("FFFOO1"), logger, false)
  };
};

#endif // QUICR_QUICR_CLIENT_SECURE_H
