#include "sg/identity/app/account_service.hpp"

#include <sg/common/errors.hpp>
#include <sg/common/log.hpp>
#include <sg/common/validation.hpp>

namespace sg::identity::app {

AccountService::AccountService(AccountRepository& accounts, const TaxonomyProvider& taxonomy,
                               const sg::common::TokenIssuer& issuer, sg::common::PasswordHashParams hash_params)
    : accounts_(accounts),
      taxonomy_(taxonomy),
      issuer_(issuer),
      hash_params_(hash_params),
      // Verified instead of a real hash when the handle does not exist, so both failure paths cost
      // one Argon2id computation with the same parameters.
      dummy_hash_(sg::common::hash_password(sg::common::random_token_b64url(24), hash_params)) {}

AuthResult AccountService::register_account(const domain::RegistrationInput& input) {
  const auto taxonomy = taxonomy_.current();
  const bool taxonomy_known = taxonomy && taxonomy->usable();
  const domain::Registration registration =
      domain::validate_registration(input, taxonomy_known ? taxonomy.get() : nullptr);
  if (!taxonomy_known) {
    // Category and branch cannot be checked against graph-service's taxonomy yet.
    throw sg::common::unavailable("Taxonomy not available yet, retry shortly");
  }
  const std::string hash = sg::common::hash_password(registration.password, hash_params_);
  domain::Account account = accounts_.create(registration, hash);
  sg::common::log_info("account registered", {{"user_id", account.id}});
  auto issued = issuer_.issue(account.id, account.handle, account.name);
  return {std::move(issued.token), issued.expires_at, std::move(account)};
}

AuthResult AccountService::login(const std::string& handle, const std::string& password) {
  const std::string normalized = sg::common::normalize_handle(handle);
  std::optional<domain::Account> account;
  if (sg::common::is_valid_handle(normalized) && password.size() <= sg::common::kPasswordMaxBytes) {
    account = accounts_.find_by_handle(normalized);
  }
  if (!account) {
    (void)sg::common::verify_password(dummy_hash_, password.substr(0, sg::common::kPasswordMaxBytes));
    throw sg::common::invalid_credentials();
  }
  if (!sg::common::verify_password(account->password_hash, password)) {
    throw sg::common::invalid_credentials();
  }
  std::optional<std::string> upgraded;
  if (sg::common::password_needs_rehash(account->password_hash, hash_params_)) {
    upgraded = sg::common::hash_password(password, hash_params_);
  }
  accounts_.record_login(account->id, upgraded);
  if (upgraded) {
    account->password_hash = *upgraded;
  }
  auto issued = issuer_.issue(account->id, account->handle, account->name);
  return {std::move(issued.token), issued.expires_at, std::move(*account)};
}

domain::Account AccountService::me(std::int64_t account_id) {
  auto account = accounts_.find_by_id(account_id);
  if (!account) {
    throw sg::common::unauthenticated("Account no longer exists");
  }
  return std::move(*account);
}

}  // namespace sg::identity::app
