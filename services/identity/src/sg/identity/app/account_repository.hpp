#pragma once

/// @file account_repository.hpp
/// Persistence port of the identity application layer.

#include <cstdint>
#include <optional>
#include <string>

#include "sg/identity/domain/account.hpp"
#include "sg/identity/domain/registration.hpp"

namespace sg::identity::app {

class AccountRepository {
 public:
  virtual ~AccountRepository() = default;

  /// Stores a new account and its `user.registered` event atomically (one transaction).
  /// @throws sg::common::ApiError 409 conflict when the handle is already taken.
  virtual domain::Account create(const domain::Registration& registration, const std::string& password_hash) = 0;

  [[nodiscard]] virtual std::optional<domain::Account> find_by_handle(const std::string& handle) = 0;
  [[nodiscard]] virtual std::optional<domain::Account> find_by_id(std::int64_t id) = 0;

  /// Records a successful login; replaces the password hash when `new_password_hash` is set
  /// (transparent upgrade to the current hashing parameters).
  virtual void record_login(std::int64_t id, const std::optional<std::string>& new_password_hash) = 0;
};

}  // namespace sg::identity::app
