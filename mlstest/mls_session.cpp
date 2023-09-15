#include "mls_session.h"
#include <iostream>

using namespace mls;

MLSInitInfo::MLSInitInfo(CipherSuite suite_in,
                         std::string user_id_in,
                         std::string group_id_in)
  : suite(suite_in)
  , user_id(std::move(user_id_in))
  , group_id(std::move(group_id_in))
  , init_key(HPKEPrivateKey::generate(suite))
  , encryption_key(HPKEPrivateKey::generate(suite))
  , signature_key(SignaturePrivateKey::generate(suite))
  , credential(Credential::basic(from_ascii(user_id)))
{
  auto leaf_node = LeafNode{ suite,
                             encryption_key.public_key,
                             signature_key.public_key,
                             credential,
                             Capabilities::create_default(),
                             Lifetime::create_default(),
                             ExtensionList{},
                             signature_key };

  key_package = KeyPackage{
    suite, init_key.public_key, leaf_node, ExtensionList{}, signature_key
  };
}

std::unique_ptr<MLSSession>
MLSSession::create(const MLSInitInfo& info)
{
  auto mls_state = State{ from_ascii(info.group_id),  info.suite,
                          info.encryption_key,        info.signature_key,
                          info.key_package.leaf_node, {} };
  return std::make_unique<MLSSession>(std::move(mls_state));
}

std::unique_ptr<MLSSession>
MLSSession::join(const MLSInitInfo& info, const bytes& welcome_data)
{
  const auto welcome = tls::get<mls::Welcome>(welcome_data);
  auto state = State{ info.init_key,
                      info.encryption_key,
                      info.signature_key,
                      info.key_package,
                      welcome,
                      std::nullopt,
                      {} };
  return std::make_unique<MLSSession>(std::move(state));
}

MLSSession::MLSSession(mls::State&& state)
  : mls_state(state)
{
}

bytes
MLSSession::fresh_secret() const
{
  return random_bytes(mls_state.cipher_suite().secret_size());
}

const mls::State&
MLSSession::get_state() const
{
  return mls_state;
}

std::tuple<bytes, bytes>
MLSSession::add(const bytes& key_package_data)
{
  const auto key_package = tls::get<KeyPackage>(key_package_data);
  const auto add_proposal = mls_state.add_proposal(key_package);

  const auto commit_opts = CommitOpts{ { add_proposal }, true, false, {} };
  const auto [commit, welcome, next_state] =
    mls_state.commit(fresh_secret(), commit_opts, {});

  const auto commit_data = tls::marshal(commit);
  const auto welcome_data = tls::marshal(welcome);

  // XXX(RLB): We should await some sort of confirmation that our commit is
  // accepted before advancing the state.  For now, we assume that the commit is
  // successful.
  mls_state = next_state;

  return std::make_tuple(welcome_data, commit_data);
}
