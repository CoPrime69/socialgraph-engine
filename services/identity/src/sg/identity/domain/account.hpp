#pragma once

/// @file account.hpp
/// The identity domain model.

#include <cstdint>
#include <optional>
#include <string>

namespace sg::identity::domain {

/// A member who can sign in. Ids are assigned by identity-service (>= 100,000,000, contracts §10.2).
struct Account {
  std::int64_t id = 0;
  std::string handle;         ///< lower-case, unique
  std::string name;           ///< display name
  std::string password_hash;  ///< Argon2id PHC string; never leaves the service
  std::int64_t created_at = 0;  ///< Unix seconds
  std::optional<std::int64_t> last_login_at;
};

/// The seeded demo member (contracts §10.2).
inline constexpr std::int64_t kDemoAccountId = 100'000'000;
inline constexpr const char* kDemoHandle = "demo";
inline constexpr const char* kDemoName = "Demo Member";

}  // namespace sg::identity::domain
