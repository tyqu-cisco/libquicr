#pragma once
#include <bytes/bytes.h>
#include <hpke/random.h>
#include <mls/common.h>
#include <mls/state.h>

#include <memory>

// Information needed per user to populate MLS state
struct MLSInitInfo
{
  mls::CipherSuite suite;
  mls::KeyPackage key_package;
  mls::HPKEPrivateKey init_key;
  mls::HPKEPrivateKey encryption_key;
  mls::SignaturePrivateKey signature_key;
  mls::Credential credential;

  MLSInitInfo(mls::CipherSuite suite, uint32_t user_id);
};

struct ParsedJoinRequest
{
  uint32_t user_id;
  mls::KeyPackage key_package;
};

struct ParsedLeaveRequest
{
  uint32_t user_id;
  mls::epoch_t epoch;
  mls::LeafIndex removed;
};

class MLSSession
{
public:
  // Set up MLS state for the creator
  static MLSSession create(const MLSInitInfo& info, uint64_t group_id);

  // Join logic
  static std::optional<MLSSession> join(const MLSInitInfo& info,
                                        const bytes& welcome_data);
  static ParsedJoinRequest parse_join(const bytes& join_data);

  // Leave logic
  bytes leave();
  std::optional<ParsedLeaveRequest> parse_leave(const bytes& leave_data);

  // Form a commit
  std::tuple<bytes, bytes> commit(
    bool force_path,
    const std::vector<ParsedJoinRequest>& joins,
    const std::vector<ParsedLeaveRequest>& leaves);

  // Whether this client should commit in a given situation
  bool should_commit(size_t n_adds,
                     const std::vector<ParsedLeaveRequest>& leaves) const;

  // Vote handling
  enum struct VoteType : uint8_t
  {
    commit = 0x01,
  };
  struct Vote
  {
    VoteType type;
    uint64_t id;
    uint32_t vote;

    TLS_SERIALIZABLE(type, id, vote);
  };
  bytes wrap_vote(const Vote& vote);
  Vote unwrap_vote(const bytes& vote_data);

  // Commit handling
  enum struct HandleResult : uint8_t
  {
    ok,
    fail,
    stale,
    future,
    removes_me,
  };
  HandleResult handle(const bytes& commit_data);

  // Access to the underlying MLS state
  const mls::State& get_state() const;
  size_t member_count() const;

private:
  MLSSession(mls::State&& state);
  bytes fresh_secret() const;

  static uint32_t user_id_from_cred(const mls::Credential& cred);

  mls::State mls_state;
  std::optional<bytes> cached_commit;
  std::optional<mls::State> cached_next_state;

  static const mls::MessageOpts message_opts;
};
