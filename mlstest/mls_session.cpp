#include "mls_session.h"
#include <iostream>

using namespace mls;

MLSInitInfo::MLSInitInfo(CipherSuite suite_in, uint32_t user_id_in)
  : suite(suite_in)
  , init_key(HPKEPrivateKey::generate(suite))
  , encryption_key(HPKEPrivateKey::generate(suite))
  , signature_key(SignaturePrivateKey::generate(suite))
  , credential(Credential::basic(tls::marshal(user_id_in)))
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

MLSSession
MLSSession::create(const MLSInitInfo& info, uint64_t group_id)
{
  auto mls_state = State{ tls::marshal(group_id),     info.suite,
                          info.encryption_key,        info.signature_key,
                          info.key_package.leaf_node, {} };
  return { std::move(mls_state) };
}

MLSSession
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
  return { std::move(state) };
}

bool
MLSSession::welcome_match(const bytes& welcome_data,
                          const KeyPackage& key_package)
{
  const auto welcome = tls::get<Welcome>(welcome_data);
  return welcome.find(key_package).has_value();
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

  // XXX(RLB): This logic assumes that the commit succeeds, and is adopted by
  // the group.  This property needs to be assured by the application logic in
  // MLSClient.
  mls_state = next_state;

  return std::make_tuple(welcome_data, commit_data);
}

MLSSession::HandleResult
MLSSession::handle(const bytes& commit_data)
{
  const auto commit = tls::get<MLSMessage>(commit_data);

  // Extract the epoch from the Commit message
  const auto get_epoch = mls::overloaded{
    [](const PublicMessage& msg) -> epoch_t { return msg.get_epoch(); },
    [](const PrivateMessage& msg) -> epoch_t { return msg.get_epoch(); },
    [](const auto& /* other */) -> epoch_t {
      throw std::runtime_error("Illegal message type");
    }
  };
  const auto commit_epoch = var::visit(get_epoch, commit.message);

  // Validate the epoch, and handle the Commit if it is timely
  const auto current_epoch = mls_state.epoch();
  if (current_epoch > commit_epoch) {
    return HandleResult::stale;
  }

  if (current_epoch < commit_epoch) {
    // TODO(RLB): It would be nice to handle this with a reordering queue.
    return HandleResult::future;
  }

  mls_state = tls::opt::get(mls_state.handle(commit));
  return HandleResult::ok;
}

const mls::State&
MLSSession::get_state() const
{
  return mls_state;
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
