#pragma once

/// @file registration.hpp
/// Registration rules (contracts §3 and §10.4).

#include <cstddef>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "sg/identity/domain/account.hpp"
#include "sg/identity/domain/taxonomy.hpp"

namespace sg::identity::domain {

inline constexpr std::size_t kTaxonomyNameMaxLength = 64;
inline constexpr std::size_t kLocationMaxLength = 100;
inline constexpr std::size_t kAboutMaxLength = 500;

/// Raw registration fields as received (type-checked, not yet validated). `influence` is no
/// longer accepted from clients (contracts §10.4) and therefore not represented.
struct RegistrationInput {
  std::optional<std::string> handle;
  std::optional<std::string> name;
  std::optional<std::string> password;
  std::optional<std::string> category;
  std::optional<std::string> branch;
  std::optional<std::string> location;
  std::optional<std::string> about;
};

/// A validated, normalised registration.
struct Registration {
  std::string handle;  ///< lower-cased
  std::string name;    ///< trimmed
  std::string password;
  std::string category;
  std::string branch;
  std::optional<std::string> location;  ///< trimmed; empty -> nullopt
  std::optional<std::string> about;     ///< trimmed; empty -> nullopt
};

/// Validates every field and reports all failures at once.
///
/// * handle: `^[a-z0-9_]{3,32}$` after lower-casing
/// * name: 1-64 characters after trimming, no control characters
/// * password: 8-128 bytes
/// * category / branch: 1-64 characters; when `taxonomy` is given, must exist in it
/// * location: optional, at most 100 characters, no control characters
/// * about: optional, at most 500 characters, no control characters except line breaks and tabs
///
/// @throws sg::common::ApiError 422 validation_failed with one detail per failing field.
[[nodiscard]] Registration validate_registration(const RegistrationInput& input, const Taxonomy* taxonomy);

/// Payload of the `user.registered` event (contracts §10.4):
/// `{ user_id, handle, name, category, branch, location, about }` (location/about may be null).
[[nodiscard]] nlohmann::json user_registered_payload(const Account& account, const Registration& registration);

}  // namespace sg::identity::domain
