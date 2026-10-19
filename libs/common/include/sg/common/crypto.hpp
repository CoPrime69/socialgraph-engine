#pragma once

/// @file crypto.hpp
/// Password hashing (Argon2id via libsodium), random tokens and base64 helpers.

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sg::common {

/// Argon2id cost parameters (libsodium always uses parallelism 1).
struct PasswordHashParams {
  unsigned long long opslimit;  ///< iterations (t)
  std::size_t memlimit;         ///< bytes (m)

  /// OWASP Password Storage Cheat Sheet: Argon2id, m = 19 MiB, t = 2, p = 1.
  static PasswordHashParams owasp();
  /// libsodium minimums. Only for tests and fast local seeding, never for real accounts.
  static PasswordHashParams fast_for_tests();
};

/// Argon2id hash in the PHC string format ($argon2id$v=19$m=...,t=...,p=1$salt$hash), random salt.
/// @throws std::invalid_argument for passwords over 4 KiB; std::runtime_error when out of memory.
[[nodiscard]] std::string hash_password(std::string_view password, const PasswordHashParams& params);

/// Verifies `password` against a stored PHC string. False for a wrong password or a malformed hash.
[[nodiscard]] bool verify_password(std::string_view stored_hash, std::string_view password);

/// True when `stored_hash` was produced with parameters other than `params` (rehash on next login).
[[nodiscard]] bool password_needs_rehash(std::string_view stored_hash, const PasswordHashParams& params);

/// `bytes` cryptographically random bytes, base64url without padding.
[[nodiscard]] std::string random_token_b64url(std::size_t bytes);

/// `bytes` cryptographically random bytes as lower-case hex.
[[nodiscard]] std::string random_hex(std::size_t bytes);

/// Fills `out` with cryptographically random bytes.
void random_bytes(std::span<unsigned char> out);

/// Constant-time comparison of two byte strings (length is not secret).
[[nodiscard]] bool constant_time_equals(std::string_view a, std::string_view b);

/// base64url (RFC 4648 §5) without padding.
[[nodiscard]] std::string base64url_encode(std::span<const unsigned char> data);
[[nodiscard]] std::string base64url_encode(std::string_view data);
/// Strict decoder: rejects padding, characters outside the alphabet and non-canonical trailing
/// bits. @throws std::invalid_argument.
[[nodiscard]] std::vector<unsigned char> base64url_decode(std::string_view text);

/// Standard base64 (RFC 4648 §4) with padding.
[[nodiscard]] std::string base64_encode(std::span<const unsigned char> data);
/// Accepts standard base64 with or without padding. @throws std::invalid_argument.
[[nodiscard]] std::vector<unsigned char> base64_decode(std::string_view text);

/// Decodes key material given as standard base64 (padded or not) or base64url.
/// @throws std::invalid_argument when undecodable.
[[nodiscard]] std::vector<unsigned char> decode_key_b64(std::string_view text);

}  // namespace sg::common
