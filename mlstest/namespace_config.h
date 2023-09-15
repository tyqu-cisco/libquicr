#pragma once
#include <quicr/quicr_common.h>

// TODO(RLB): Configure this information based on manifest information
// TODO(CC): use numero-uno library to convert from uri to hex representation

/*
 * URI Sample:
 * "quicr://webex.cisco.com<pen=1><sub_pen=1>/conferences/<int24>/secGroupId/<int16>/datatype/<int8>/endpoint/<int24>
 * webex.cisco.com, 32 bits = 0xAABBCCDD
 * conference,      24 bits = 0x112233
 * secGroupId,      16 bits = 0xEEEE
 * datatype,         8 bits = one-of {KeyPackage(0x01), Welcome(0x02),
 * Commit(0x03} endpointId       24 bits = 0x000001 - creator, 0x000002 onwards
 * for participants messageId        24 bits for each message
 *
 */
struct NamespaceConfig
{
  quicr::Namespace key_package;
  quicr::Namespace welcome;
  quicr::Namespace commit;

  static NamespaceConfig create_default()
  {
    return {
      .key_package =
        quicr::Namespace(quicr::Name("0xAABBCCDD112233EEEE01000002FFFF01"), 80),
      .welcome =
        quicr::Namespace(quicr::Name("0xAABBCCDD112233EEEE02000002FFFF01"), 80),
      .commit = quicr::Namespace(
        quicr::Name("0xAABBCCDD112233EEEE03000001FFFF01"), 80),

    };
  }
};
