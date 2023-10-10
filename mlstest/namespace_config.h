#pragma once
#include <mls/core_types.h>
#include <quicr/namespace.h>

// # Namespace Structure
//
// Assumptions:
// * Each MLS group has a 56-bit globally unique ID
// * Each client has a 32-bit ID that is unique within the scope of the group,
//   through the whole lifetime of the group.
//
// For advertising a KeyPackage:
//
//        group_id       op    sender      kp_id
//  -------------------- -- ----------- -----------
// |XX|XX|XX|XX|XX|XX|XX|01|XX|XX|XX|XX|XX|XX|XX|XX|
//
// * On connect:
//   * Subscribe to group_id/01/*
//   * Publish intent for group_id/01/sender/*
// * On join:
//   * Publish KeyPackage to group_id/01/sender/kp_id
//     * kp_id = H(KeyPackage)[0:4]
//
// For responding to a KeyPackage with a Welcome:
//
//        group_id       op    sender      kp_id
//  -------------------- -- ----------- -----------
// |XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|
//
// * On connect:
//   * Subscribe to group_id/02/*
//   * Publish intent for group_id/02/sender/*
// * On commit covering group_id/01/joiner/kp_id:
//   * Send Welcome to group_id/02/sender/kp_id
//
// For sending a Commit:
//
//        group_id       op    sender      epoch
//  -------------------- -- ----------- -----------
// |XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|
//
// * On connect:
//   * Subscribe to group_id/03/*
//   * Publish intent for group_id/03/sender/*
// * On commit:
//   * Send Welcome to group_id/03/sender/epoch
//
// For requesting to be removed from the group:
//
//        group_id       op    sender        0
//  -------------------- -- ----------- -----------
// |XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|XX|
//
// * On connect:
//   * Subscribe to group_id/04/*
//   * Publish intent for group_id/04/sender/*
// * On leave:
//   * Send Welcome to group_id/04/sender/0
struct SubNamespace
{
  quicr::Namespace ns;

  SubNamespace();
  SubNamespace(quicr::Namespace ns_in);

  operator quicr::Namespace() const;

  // Extend by a specific number of bits (no more than 63)
  SubNamespace extend(uint64_t value, uint8_t bits) const;

private:
  static const size_t name_width = 128;
};

struct NamespaceConfig
{
  NamespaceConfig(uint64_t group_id);

  struct Operation
  {
    using Type = uint8_t;
    static const Type key_package = 0x01;
    static const Type welcome = 0x02;
    static const Type commit = 0x03;
    static const Type leave = 0x04;
    static const Type commit_vote = 0x05;
  };

  // Namespaces to subscribe to
  quicr::Namespace key_package_sub() const;
  quicr::Namespace welcome_sub() const;
  quicr::Namespace commit_sub() const;
  quicr::Namespace leave_sub() const;

  // Namespaces to publish within
  quicr::Namespace key_package_pub(uint32_t sender) const;
  quicr::Namespace welcome_pub(uint32_t sender) const;
  quicr::Namespace commit_pub(uint32_t sender) const;
  quicr::Namespace leave_pub(uint32_t sender) const;

  // Form specific names
  static uint32_t id_for(const mls::KeyPackage& key_package);
  quicr::Name for_key_package(uint32_t sender, uint32_t key_package_id) const;
  quicr::Name for_welcome(uint32_t sender, uint32_t key_package_id) const;
  quicr::Name for_commit(uint32_t sender, uint64_t epoch) const;
  quicr::Name for_leave(uint32_t sender) const;

  // Parse names
  std::tuple<Operation::Type, uint32_t, uint32_t> parse(quicr::Name name) const;

private:
  static const size_t prefix_bits = 56;
  static const size_t op_bits = 8;
  static const size_t sender_bits = 32;
  static const size_t key_package_id_bits = 32;
  static const size_t epoch_bits = 32;

  SubNamespace key_package_base;
  SubNamespace welcome_base;
  SubNamespace commit_base;
  SubNamespace leave_base;
};
