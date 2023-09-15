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
  std::string user_id;
  std::string group_id;
  mls::KeyPackage key_package;
  mls::HPKEPrivateKey init_key;
  mls::HPKEPrivateKey encryption_key;
  mls::SignaturePrivateKey signature_key;
  mls::Credential credential;

  // XXX(RLB): This should be deleted ASAP
  MLSInitInfo() = default;
  MLSInitInfo(mls::CipherSuite suite,
              std::string user_id,
              std::string group_id);
};

class MLSSession
{
public:
  // setup mls state for the creator
  static std::unique_ptr<MLSSession> create(const MLSInitInfo& info);

  // setup mls state for the joiners
  static std::unique_ptr<MLSSession> join(const MLSInitInfo& info,
                                          const bytes& welcome_data);

  // group creator
  std::tuple<bytes, bytes> add(const bytes& key_package_data);
  const mls::State& get_state() const;

  // XXX(RLB) This ctor should be private, but has to be public so that
  // std::make_unique can use it.
  MLSSession(mls::State&& state);

private:
  bytes fresh_secret() const;

  mls::State mls_state;
};
