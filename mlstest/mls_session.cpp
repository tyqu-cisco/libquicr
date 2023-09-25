#include "mls_session.h"

#include <numeric>
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

const MessageOpts MLSSession::message_opts = {
  .encrypt = true,
  .authenticated_data = {},
  .padding_size = 0,
};

MLSSession
MLSSession::create(const MLSInitInfo& info, uint64_t group_id)
{
  auto mls_state = State{ tls::marshal(group_id),     info.suite,
                          info.encryption_key,        info.signature_key,
                          info.key_package.leaf_node, {} };
  return { std::move(mls_state) };
}

std::optional<MLSSession>
MLSSession::join(const MLSInitInfo& info, const bytes& welcome_data)
{
  const auto welcome = tls::get<mls::Welcome>(welcome_data);
  if (!welcome.find(info.key_package)) {
    return std::nullopt;
  }

  auto state = State{ info.init_key,
                      info.encryption_key,
                      info.signature_key,
                      info.key_package,
                      welcome,
                      std::nullopt,
                      {} };
  return { { std::move(state) } };
}

std::optional<std::tuple<bytes, bytes>>
MLSSession::add(uint32_t user_id, const bytes& key_package_data)
{
  const auto key_package = tls::get<KeyPackage>(key_package_data);
  if (!credential_matches_id(user_id, key_package.leaf_node.credential)) {
    // KeyPackage is not for the identified user
    return std::nullopt;
  }

  const auto add_proposal = mls_state.add_proposal(key_package);

  const auto commit_opts = CommitOpts{ { add_proposal }, true, false, {} };
  const auto [commit, welcome, next_state] =
    mls_state.commit(fresh_secret(), commit_opts, message_opts);

  const auto commit_data = tls::marshal(commit);
  const auto welcome_data = tls::marshal(welcome);

  // XXX(RLB): This logic assumes that the commit succeeds, and is adopted by
  // the group.  This property needs to be assured by the application logic in
  // MLSClient.
  mls_state = next_state;
  return std::make_tuple(welcome_data, commit_data);
}

bytes
MLSSession::leave()
{
  const auto remove_proposal = mls_state.remove(mls_state.index(), {});
  return tls::marshal(remove_proposal);
}

std::optional<LeafIndex>
MLSSession::validate_leave(uint32_t user_id, const bytes& remove_data)
{
  // Import the message
const auto remove_message = tls::get<MLSMessage>(remove_data);
  const auto remove_auth_content = mls_state.unwrap(remove_message);
  const auto& remove_content = remove_auth_content.content;

  // Verify that this is a self-remove proposal
  const auto& remove_proposal = var::get<Proposal>(remove_content.content);
  const auto& remove = var::get<Remove>(remove_proposal.content);
  const auto& sender =
    var::get<MemberSender>(remove_content.sender.sender).sender;
  if (remove.removed != sender) {
    // Remove proposal is not self-remove
    return std::nullopt;
  }

  // Verify that the self-removed user has the indicated user ID
  const auto leaf = mls_state.tree().leaf_node(remove.removed).value();
  if (!credential_matches_id(user_id, leaf.credential)) {
    // Remove proposal is not for the identified user
    return std::nullopt;
  }

  return remove.removed;
}

bytes
MLSSession::remove(LeafIndex removed)
{
  // Re-originate the remove proposal and commit it
  const auto remove_proposal = mls_state.remove_proposal(removed);
  const auto commit_opts =
    CommitOpts{ { remove_proposal }, true, false, {} };
  const auto [commit, _welcome, next_state] =
    mls_state.commit(fresh_secret(), commit_opts, message_opts);
  mls::silence_unused(_welcome);

  const auto commit_data = tls::marshal(commit);

  // XXX(RLB): This logic assumes that the commit succeeds, and is adopted by
  // the group.  This property needs to be assured by the application logic in
  // MLSClient.
  mls_state = next_state;
  return commit_data;
}

static std::vector<LeafIndex>
add_locations(size_t n_adds, const TreeKEMPublicKey& tree)
{
  auto to_place = n_adds;
  auto places = std::vector<LeafIndex>{};
  for (auto i = LeafIndex{ 0 }; to_place > 0; i.val++) {
    if (i < tree.size && !tree.node_at(i).blank()) {
      continue;
    }

    places.push_back(i);
    to_place -= 1;
  }

  return places;
}

static uint32_t
topological_distance(LeafIndex a, LeafIndex b)
{
  return a.ancestor(b).level();
}

static uint32_t
total_distance(LeafIndex a, const std::vector<LeafIndex>& b)
{
  return std::accumulate(b.begin(), b.end(), 0, [&](auto last, auto bx) {
    return last + topological_distance(a, bx);
  });
}

// XXX(RLB) This method currently returns a boolean, but we might want to have
// it return the raw distance metric.  This would support a "jump ball" commit
// strategy, where the closest nodes in the tree commit fastest.
bool
MLSSession::should_commit(size_t n_adds, const std::vector<LeafIndex>& removed) const
{
  // A node should commit if:
  //
  // * It has the lowest total topological distance to the changes among all
  //   non-blank leaf nodes.
  // * No node to its left has the same topological distance.
  //
  // We compute this in one pass through the leaves of the tree by computing the
  // total topological distance at each leaf node and updating only if the
  // distance is lowest than the lowest known.

  auto affected = add_locations(n_adds, mls_state.tree());
  affected.insert(affected.end(), removed.begin(), removed.end());

  auto min_index = std::optional<LeafIndex>{};
  auto min_dist = std::optional<uint32_t>{};
  mls_state.tree().all_leaves([&](auto i, const auto& /* unused */) {
    if (std::find(removed.begin(), removed.end(), i) != removed.end()) {
      // A removed leaf can't commit
      return true;
    }

    const auto dist = total_distance(i, affected);
    if (min_dist && dist >= min_dist) {
      // If this node is non-minimal, keep looking
      return true;
    }

    min_index = i;
    min_dist = dist;
    return true;
  });

  return mls_state.index() == min_index;
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

size_t
MLSSession::member_count() const
{
  size_t members = 0;
  mls_state.tree().all_leaves([&](auto /* i */, const auto& /* leaf */) {
    members += 1;
    return true;
  });
  return members;
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

bool
MLSSession::credential_matches_id(uint32_t user_id, const Credential& cred)
{
  const auto basic_cred = cred.get<BasicCredential>();
  const auto expected_identity = tls::marshal(user_id);
  return basic_cred.identity == expected_identity;
}
