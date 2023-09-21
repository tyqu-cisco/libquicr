#include <doctest/doctest.h>

#include "namespace_config.h"

TEST_CASE("Namespace Config")
{
  using quicr::Namespace;
  const auto namespaces = NamespaceConfig(0x01020304050607);

  REQUIRE(namespaces.key_package_sub() ==
          Namespace("0x01020304050607010000000000000000/64"));
  REQUIRE(namespaces.welcome_sub() ==
          Namespace("0x01020304050607020000000000000000/64"));
  REQUIRE(namespaces.commit_sub() ==
          Namespace("0x01020304050607030000000000000000/64"));

  const auto user_id = uint32_t(0x0a0b0c0d);
  REQUIRE(namespaces.key_package_pub(user_id) ==
          Namespace("0x01020304050607010a0b0c0d00000000/96"));
  REQUIRE(namespaces.welcome_pub(user_id) ==
          Namespace("0x01020304050607020a0b0c0d00000000/96"));
  REQUIRE(namespaces.commit_pub(user_id) ==
          Namespace("0x01020304050607030a0b0c0d00000000/96"));

  const auto third_value = uint32_t(0xf0f1f2f3);
  REQUIRE(namespaces.for_key_package(user_id, third_value) ==
          0x01020304050607010a0b0c0df0f1f2f3_name);
  REQUIRE(namespaces.for_welcome(user_id, third_value) ==
          0x01020304050607020a0b0c0df0f1f2f3_name);
  REQUIRE(namespaces.for_commit(user_id, third_value) ==
          0x01020304050607030a0b0c0df0f1f2f3_name);
}
