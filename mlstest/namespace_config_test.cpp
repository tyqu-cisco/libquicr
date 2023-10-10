#include <doctest/doctest.h>

#include "namespace_config.h"

TEST_CASE("SubNamespace")
{
  using quicr::Namespace;

  const auto ns8 = Namespace(SubNamespace{}.extend(0xff, 8));
  const auto ns16 = Namespace(SubNamespace{}.extend(0xffff, 16));
  const auto ns32 = Namespace(SubNamespace{}.extend(0xffffffff, 32));
  const auto ns64 = Namespace(SubNamespace{}.extend(0xffffffffffffffff, 64));

  REQUIRE(ns8 == Namespace("0xff000000000000000000000000000000/8"));
  REQUIRE(ns16 == Namespace("0xffff0000000000000000000000000000/16"));
  REQUIRE(ns32 == Namespace("0xffffffff000000000000000000000000/32"));
  REQUIRE(ns64 == Namespace("0xffffffffffffffff0000000000000000/64"));

  const auto full_ns = Namespace(SubNamespace{}
                                   .extend(0x0001020304050607, 64)
                                   .extend(0x08090a0b, 32)
                                   .extend(0x0c0d, 16)
                                   .extend(0x0e, 8)
                                   .extend(0x0f, 8));
  REQUIRE(full_ns == Namespace("0x000102030405060708090a0b0c0d0e0f/128"));
}

TEST_CASE("Namespace Config")
{
  using quicr::Namespace;
  const auto welcome_ns = Namespace("0x00010203040506070000000000000000/72");
  const auto group_ns = Namespace("0x00010203040506070100000000000000/72");
  const auto endpoint_id = uint32_t(0x00a0a1a2);
  auto namespaces = NamespaceConfig(welcome_ns, group_ns, endpoint_id);

  REQUIRE(namespaces.welcome_sub() == welcome_ns);
  REQUIRE(namespaces.group_sub() == group_ns);

  REQUIRE(namespaces.welcome_pub() ==
          Namespace("0x000102030405060700a0a1a200000000/96"));
  REQUIRE(namespaces.group_pub() ==
          Namespace("0x000102030405060701a0a1a200000000/96"));

  REQUIRE(namespaces.for_welcome() == 0x000102030405060700a0a1a200000000_name);
  REQUIRE(namespaces.for_welcome() == 0x000102030405060700a0a1a200000001_name);
  REQUIRE(namespaces.for_welcome() == 0x000102030405060700a0a1a200000002_name);

  REQUIRE(namespaces.for_group() == 0x000102030405060701a0a1a200000000_name);
  REQUIRE(namespaces.for_group() == 0x000102030405060701a0a1a200000001_name);
  REQUIRE(namespaces.for_group() == 0x000102030405060701a0a1a200000002_name);
}
