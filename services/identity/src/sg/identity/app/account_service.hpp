#pragma once

/// @file account_service.hpp
/// Identity use cases: register, login, me (contracts §4, §10.4).

#include <cstdint>
#include <string>

#include <sg/common/auth.hpp>
#include <sg/common/crypto.hpp>

#include "sg/identity/app/account_repository.hpp"
#include "sg/identity/app/taxonomy_provider.hpp"
#include "sg/identity/domain/account.hpp"
#include "sg/identity/domain/registration.hpp"

namespace sg::identity::app {

/// A signed-in member: access token plus the account.
struct AuthResult {
  std::string token;
  std::int64_t expires_at = 0;  ///< Unix seconds
  domain::Account account;
};

class AccountService {
 public:
  /// The references must outlive the service.
  AccountService(AccountRepository& accounts, const TaxonomyProvider& taxonomy, const sg::common::TokenIssuer& issuer,
                 sg::common::PasswordHashParams hash_params);

  /// Validates (422), checks the taxonomy is known (503 otherwise), hashes the password with
  /// Argon2id, stores account + `user.registered` event atomically (409 on a taken handle) and
  /// returns a token.
  AuthResult register_account(const domain::RegistrationInput& input);

  /// Verifies the credentials. Unknown handle and wrong password fail identically (401
  /// invalid_credentials) and take similar time: a dummy hash is verified for unknown handles.
  AuthResult login(const std::string& handle, const std::string& password);

  /// The account of an authenticated caller. @throws ApiError 401 when it no longer exists.
  domain::Account me(std::int64_t account_id);

 private:
  AccountRepository& accounts_;
  const TaxonomyProvider& taxonomy_;
  const sg::common::TokenIssuer& issuer_;
  sg::common::PasswordHashParams hash_params_;
  std::string dummy_hash_;
};

}  // namespace sg::identity::app
