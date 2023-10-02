#include "mls_session.h"

#include <iostream>
#include <numeric>
#include <set>

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

ParsedJoinRequest
MLSSession::parse_join(const bytes& join_data)
{
  const auto key_package = tls::get<KeyPackage>(join_data);
  const auto user_id = user_id_from_cred(key_package.leaf_node.credential);
  return { user_id, key_package };
}

bytes
MLSSession::leave()
{
  const auto remove_proposal = mls_state.remove(mls_state.index(), {});
  return tls::marshal(remove_proposal);
}

std::optional<ParsedLeaveRequest>
MLSSession::parse_leave(const bytes& leave_data)
{
  // Import the message
  const auto leave_message = tls::get<MLSMessage>(leave_data);
  if (leave_message.group_id() != mls_state.group_id()) {
    return std::nullopt;
  }

  const auto epoch = leave_message.epoch();
  if (epoch != mls_state.epoch()) {
    return std::nullopt;
  }

  const auto leave_auth_content = mls_state.unwrap(leave_message);
  const auto& leave_content = leave_auth_content.content;
  const auto& leave_sender = leave_content.sender.sender;

  // Verify that this is a self-remove proposal
  const auto& remove_proposal = var::get<Proposal>(leave_content.content);
  const auto& remove = var::get<Remove>(remove_proposal.content);
  const auto& sender = var::get<MemberSender>(leave_sender).sender;
  if (remove.removed != sender) {
    return std::nullopt;
  }

  // Verify that the self-removed user has the indicated user ID
  const auto leaf = mls_state.tree().leaf_node(remove.removed).value();
  const auto user_id = user_id_from_cred(leaf.credential);

  return { { user_id, epoch, remove.removed } };
}

std::tuple<bytes, bytes>
MLSSession::commit(bool force_path,
                   const std::vector<ParsedJoinRequest>& joins,
                   const std::vector<ParsedLeaveRequest>& leaves)
{
  auto proposals = std::vector<Proposal>{};

  std::transform(
    joins.begin(),
    joins.end(),
    std::back_inserter(proposals),
    [&](const auto& req) { return mls_state.add_proposal(req.key_package); });

  std::transform(
    leaves.begin(),
    leaves.end(),
    std::back_inserter(proposals),
    [&](const auto& req) { return mls_state.remove_proposal(req.removed); });

  const auto commit_opts = CommitOpts{ proposals, true, force_path, {} };
  const auto [commit, welcome, next_state] =
    mls_state.commit(fresh_secret(), commit_opts, message_opts);

  const auto commit_data = tls::marshal(commit);
  const auto welcome_data = tls::marshal(welcome);

  cached_commit = commit_data;
  cached_next_state = std::move(next_state);
  return std::make_tuple(commit_data, welcome_data);
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
MLSSession::should_commit(size_t n_adds,
                          const std::vector<ParsedLeaveRequest>& leaves) const
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

  auto removed = std::set<mls::LeafIndex>{};
  std::transform(leaves.begin(),
                 leaves.end(),
                 std::inserter(removed, removed.begin()),
                 [](const auto& req) { return req.removed; });

  auto affected = add_locations(n_adds, mls_state.tree());
  affected.insert(affected.end(), removed.begin(), removed.end());

  auto min_index = std::optional<LeafIndex>{};
  auto min_dist = std::optional<uint32_t>{};
  mls_state.tree().all_leaves([&](auto i, const auto& /* unused */) {
    if (removed.contains(i)) {
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

bytes
MLSSession::wrap_vote(const Vote& vote)
{
  const auto vote_data = tls::marshal(vote);
  const auto message = mls_state.protect({}, vote_data, 0);
  return tls::marshal(message);
}

MLSSession::Vote
MLSSession::unwrap_vote(const bytes& vote_data)
{
  const auto message = tls::get<MLSMessage>(vote_data);
  const auto [_aad, pt] = mls_state.unprotect(message);
  return tls::get<Vote>(pt);
}

MLSSession::HandleResult
MLSSession::handle(const bytes& commit_data)
{
  if (commit_data == cached_commit) {
    mls_state = std::move(cached_next_state.value());
    cached_commit.reset();
    cached_next_state.reset();
    return HandleResult::ok;
  }

  const auto commit_message = tls::get<MLSMessage>(commit_data);

  // Extract the epoch from the Commit message
  const auto get_epoch = mls::overloaded{
    [](const PublicMessage& msg) -> epoch_t { return msg.get_epoch(); },
    [](const PrivateMessage& msg) -> epoch_t { return msg.get_epoch(); },
    [](const auto& /* other */) -> epoch_t {
      throw std::runtime_error("Illegal message type");
    }
  };
  const auto commit_epoch = var::visit(get_epoch, commit_message.message);

  // Validate the epoch, and handle the Commit if it is timely
  const auto current_epoch = mls_state.epoch();
  if (current_epoch > commit_epoch) {
    return HandleResult::stale;
  }

  if (current_epoch < commit_epoch) {
    // TODO(RLB): It would be nice to handle this with a reordering queue.
    return HandleResult::future;
  }

  // Attempt to handle the Commit
  // XXX(richbarn): It would be nice to unwrap the Commit here and explicitly
  // check whether there is a Remove proposal removing this client.  However,
  // this causes a double-decrypt, which fails because decrypting causes keys to
  // be erased.  Instead we assume that any failure due to an invalid proposal
  // list is this type of failure
  try {
    mls_state = tls::opt::get(mls_state.handle(commit_message));
  } catch (const ProtocolError& exc) {
    if (std::string(exc.what()) == "Invalid proposal list") {
      return HandleResult::removes_me;
    }

    return HandleResult::fail;
  }

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

uint32_t
MLSSession::user_id_from_cred(const Credential& cred)
{
  const auto& basic_cred = cred.get<BasicCredential>();
  return tls::get<uint32_t>(basic_cred.identity);
}
