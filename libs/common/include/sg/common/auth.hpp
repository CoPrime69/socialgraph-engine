#pragma once

/// @file auth.hpp
/// Stateless access tokens (contracts.md §4): JWT (RFC 7519) signed with Ed25519 (`alg: EdDSA`,
/// RFC 8037), base64url without padding. identity-service holds the 32-byte seed and issues
/// tokens; every service verifies them locally with the public key.

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include <httplib.h>

namespace sg::common {

/// Verified claims of an access token.
struct Claims {
  std::int64_t user_id;   ///< "sub"
  std::string handle;     ///< "handle"
  std::string name;       ///< "name"
  std::int64_t issued_at;   ///< "iat", Unix seconds
  std::int64_t expires_at;  ///< "exp", Unix seconds
};

/// Ed25519 key material, standard base64 (with padding).
struct KeyPair {
  std::string seed_b64;        ///< AUTH_SIGNING_SEED (secret, 32 bytes)
  std::string public_key_b64;  ///< AUTH_PUBLIC_KEY (32 bytes)
};

/// Generates a fresh random key pair.
[[nodiscard]] KeyPair generate_keypair();

/// Derives the base64 public key from a base64 32-byte seed. @throws std::invalid_argument.
[[nodiscard]] std::string public_key_from_seed(std::string_view seed_b64);

/// Signs tokens. Thread-safe (immutable after construction). The secret key is wiped on destruction.
class TokenIssuer {
 public:
  /// @throws std::invalid_argument if the seed is not base64 of exactly 32 bytes or ttl <= 0.
  TokenIssuer(std::string seed_b64, std::chrono::seconds ttl, std::string issuer = "socialgraph-identity");
  ~TokenIssuer();
  TokenIssuer(const TokenIssuer&) = delete;
  TokenIssuer& operator=(const TokenIssuer&) = delete;

  struct Issued {
    std::string token;
    std::int64_t expires_at;  ///< Unix seconds
  };

  /// Token for the given user, valid for the configured TTL from now.
  [[nodiscard]] Issued issue(std::int64_t user_id, const std::string& handle, const std::string& name) const;
  /// Same, with an explicit "now" (Unix seconds); used by tests to mint expired tokens.
  [[nodiscard]] Issued issue_at(std::int64_t user_id, const std::string& handle, const std::string& name,
                                std::int64_t now) const;
  [[nodiscard]] std::string public_key_b64() const;
  [[nodiscard]] std::chrono::seconds ttl() const noexcept { return ttl_; }

 private:
  std::array<unsigned char, 64> secret_key_{};
  std::array<unsigned char, 32> public_key_{};
  std::chrono::seconds ttl_;
  std::string issuer_;
};

/// Verifies tokens. Thread-safe.
class TokenVerifier {
 public:
  /// @throws std::invalid_argument if the key is not base64 of exactly 32 bytes.
  explicit TokenVerifier(std::string public_key_b64, std::string issuer = "socialgraph-identity");

  /// Allowed clock difference between issuer and verifier.
  static constexpr std::int64_t kClockSkewSeconds = 30;
  /// Longer tokens are rejected before any parsing.
  static constexpr std::size_t kMaxTokenBytes = 4096;

  /// Verifies signature, algorithm (EdDSA only), issuer, subject and expiry (±30 s skew).
  /// @throws ApiError 401 unauthenticated for any malformed, forged, expired, wrong-algorithm or
  ///         wrong-issuer token.
  [[nodiscard]] Claims verify(std::string_view token) const;
  /// Same, against an explicit "now" (Unix seconds).
  [[nodiscard]] Claims verify_at(std::string_view token, std::int64_t now) const;

 private:
  std::array<unsigned char, 32> public_key_{};
  std::string issuer_;
};

/// Parses `Authorization: Bearer <token>` and verifies it.
/// @throws ApiError 401 unauthenticated when the header is missing, malformed or the token is invalid.
[[nodiscard]] Claims require_auth(const httplib::Request& req, const TokenVerifier& verifier);

}  // namespace sg::common
