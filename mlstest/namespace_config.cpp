#include "namespace_config.h"

using quicr::Name;
using quicr::Namespace;
namespace tls = mls::tls;

static const auto zero_name = 0x00000000000000000000000000000000_name;

SubNamespace::SubNamespace()
  : ns(zero_name, 0)
{
}

SubNamespace::SubNamespace(Namespace ns_in)
  : ns(std::move(ns_in))
{
}

SubNamespace::operator Namespace() const
{
  return ns;
}

SubNamespace
SubNamespace::extend(uint64_t value, uint8_t bits) const
{
  if (bits > 63) {
    throw std::runtime_error("Cannot extend by more than 63 bits at once");
  }

  if (ns.length() + bits > name_width) {
    throw std::runtime_error("Cannot extend name past 128 bits");
  }

  const auto new_length = ns.length() + bits;
  const auto shift = name_width - new_length;

  auto delta = zero_name;
  const auto mask = (uint64_t(1) << bits) - 1;
  delta += (value & mask);
  delta <<= shift;
  const auto new_name = ns.name() + delta;

  return SubNamespace(Namespace(new_name, new_length));
}

NamespaceConfig::NamespaceConfig(uint64_t group_id)
{
  const auto base = SubNamespace{}.extend(group_id, prefix_bits);
  key_package_base = base.extend(Operation::key_package, op_bits);
  welcome_base = base.extend(Operation::welcome, op_bits);
  commit_base = base.extend(Operation::commit, op_bits);
  leave_base = base.extend(Operation::leave, op_bits);
  commit_vote_base = base.extend(Operation::commit_vote, op_bits);
}

Namespace
NamespaceConfig::key_package_sub() const
{
  return key_package_base;
}

Namespace
NamespaceConfig::welcome_sub() const
{
  return welcome_base;
}

Namespace
NamespaceConfig::commit_sub() const
{
  return commit_base;
}

Namespace
NamespaceConfig::leave_sub() const
{
  return leave_base;
}

Namespace
NamespaceConfig::commit_vote_sub() const
{
  return commit_vote_base;
}

Namespace
NamespaceConfig::key_package_pub(uint32_t sender) const
{
  return key_package_base.extend(sender, sender_bits);
}

Namespace
NamespaceConfig::welcome_pub(uint32_t sender) const
{
  return welcome_base.extend(sender, sender_bits);
}

Namespace
NamespaceConfig::commit_pub(uint32_t sender) const
{
  return commit_base.extend(sender, sender_bits);
}

Namespace
NamespaceConfig::leave_pub(uint32_t sender) const
{
  return leave_base.extend(sender, sender_bits);
}

Namespace
NamespaceConfig::commit_vote_pub(uint32_t sender) const
{
  return commit_vote_base.extend(sender, sender_bits);
}

uint32_t
NamespaceConfig::id_for(const mls::KeyPackage& key_package)
{
  return tls::get<uint32_t>(key_package.ref());
}

Name
NamespaceConfig::for_key_package(uint32_t sender, uint32_t key_package_id) const
{
  return key_package_base.extend(sender, sender_bits)
    .extend(key_package_id, key_package_id_bits)
    .ns.name();
}

Name
NamespaceConfig::for_welcome(uint32_t sender, uint32_t key_package_id) const
{
  return welcome_base.extend(sender, sender_bits)
    .extend(key_package_id, key_package_id_bits)
    .ns.name();
}

Name
NamespaceConfig::for_commit(uint32_t sender, uint64_t epoch) const
{
  return commit_base.extend(sender, sender_bits)
    .extend(epoch, epoch_bits)
    .ns.name();
}

Name
NamespaceConfig::for_leave(uint32_t sender) const
{
  return leave_base.extend(sender, sender_bits).ns.name();
}

Name
NamespaceConfig::for_commit_vote(uint32_t sender, uint64_t epoch) const
{
  return commit_vote_base.extend(sender, sender_bits)
    .extend(epoch, epoch_bits)
    .ns.name();
}

std::tuple<NamespaceConfig::Operation::Type, uint32_t, uint32_t>
NamespaceConfig::parse(quicr::Name name) const
{
  auto third_value = uint32_t(name);
  name >>= 32;

  auto sender = uint32_t(name);
  name >>= 32;

  auto op = uint8_t(name);

  return { op, sender, third_value };
}
