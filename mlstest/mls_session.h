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

class MLSSession
{
public:
  // Set up MLS state for the creator
  static MLSSession create(const MLSInitInfo& info, uint64_t group_id);

  // Set up MLS state for a joiner
  static MLSSession join(const MLSInitInfo& info, const bytes& welcome_data);
  static bool welcome_match(const bytes& welcome_data, const mls::KeyPackage& key_package);

  // Group operations
  // TODO(RLB): Add a remove() and triggering logic
  std::tuple<bytes, bytes> add(const bytes& key_package_data);

  // Commit handling
  enum struct HandleResult : uint8_t {
    ok,
    stale,
    future,
  };
  HandleResult handle(const bytes& commit_data);

  // Access to the underlying MLS state
  const mls::State& get_state() const;

private:
  MLSSession(mls::State&& state);
  bytes fresh_secret() const;

  mls::State mls_state;
};
