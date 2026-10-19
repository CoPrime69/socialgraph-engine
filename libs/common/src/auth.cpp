#include "sg/common/auth.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>
#include <sodium.h>

#include "sg/common/crypto.hpp"
#include "sg/common/errors.hpp"
#include "sg/common/lru_cache.hpp"
#include "sg/common/time.hpp"
#include "sodium_init.hpp"

namespace sg::common {
namespace {

constexpr std::string_view kAlgorithm = "EdDSA";

std::vector<unsigned char> decode_exact(std::string_view b64, std::size_t expected, const char* what) {
  std::vector<unsigned char> bytes;
  try {
    bytes = decode_key_b64(b64);
  } catch (const std::invalid_argument&) {
    throw std::invalid_argument(std::string(what) + " is not valid base64");
  }
  if (bytes.size() != expected) {
    sodium_memzero(bytes.data(), bytes.size());
    throw std::invalid_argument(std::string(what) + " must decode to " + std::to_string(expected) + " bytes");
  }
  return bytes;
}

ApiError invalid_token() { return unauthenticated("Invalid access token"); }

/// Strict base64url decode of a JWT segment; any failure is an invalid token.
std::vector<unsigned char> decode_segment(std::string_view segment) {
  if (segment.empty()) {
    throw invalid_token();
  }
  try {
    return base64url_decode(segment);
  } catch (const std::invalid_argument&) {
    throw invalid_token();
  }
}

nlohmann::json parse_object(const std::vector<unsigned char>& bytes) {
  nlohmann::json value = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, /*allow_exceptions=*/false);
  if (!value.is_object()) {
    throw invalid_token();
  }
  return value;
}

std::int64_t integer_claim(const nlohmann::json& payload, const char* key) {
  const auto it = payload.find(key);
  if (it == payload.end() || !it->is_number_integer()) {
    throw invalid_token();
  }
  return it->get<std::int64_t>();
}

std::string string_claim(const nlohmann::json& payload, const char* key) {
  const auto it = payload.find(key);
  if (it == payload.end() || !it->is_string()) {
    throw invalid_token();
  }
  return it->get<std::string>();
}

std::int64_t parse_subject(const std::string& sub) {
  if (sub.empty() || sub.size() > 19 || !std::all_of(sub.begin(), sub.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
      })) {
    throw invalid_token();
  }
  std::int64_t id = 0;
  const auto [ptr, ec] = std::from_chars(sub.data(), sub.data() + sub.size(), id);
  if (ec != std::errc() || ptr != sub.data() + sub.size() || id <= 0) {
    throw invalid_token();
  }
  return id;
}

}  // namespace

KeyPair generate_keypair() {
  detail::ensure_sodium();
  std::array<unsigned char, crypto_sign_SEEDBYTES> seed{};
  std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> pk{};
  std::array<unsigned char, crypto_sign_SECRETKEYBYTES> sk{};
  randombytes_buf(seed.data(), seed.size());
  crypto_sign_seed_keypair(pk.data(), sk.data(), seed.data());
  KeyPair pair{base64_encode(seed), base64_encode(pk)};
  sodium_memzero(seed.data(), seed.size());
  sodium_memzero(sk.data(), sk.size());
  return pair;
}

std::string public_key_from_seed(std::string_view seed_b64) {
  detail::ensure_sodium();
  auto seed = decode_exact(seed_b64, crypto_sign_SEEDBYTES, "signing seed");
  std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> pk{};
  std::array<unsigned char, crypto_sign_SECRETKEYBYTES> sk{};
  crypto_sign_seed_keypair(pk.data(), sk.data(), seed.data());
  sodium_memzero(seed.data(), seed.size());
  sodium_memzero(sk.data(), sk.size());
  return base64_encode(pk);
}

TokenIssuer::TokenIssuer(std::string seed_b64, std::chrono::seconds ttl, std::string issuer)
    : ttl_(ttl), issuer_(std::move(issuer)) {
  detail::ensure_sodium();
  if (ttl.count() <= 0) {
    throw std::invalid_argument("token TTL must be positive");
  }
  auto seed = decode_exact(seed_b64, crypto_sign_SEEDBYTES, "signing seed");
  sodium_memzero(seed_b64.data(), seed_b64.size());
  crypto_sign_seed_keypair(public_key_.data(), secret_key_.data(), seed.data());
  sodium_memzero(seed.data(), seed.size());
}

TokenIssuer::~TokenIssuer() { sodium_memzero(secret_key_.data(), secret_key_.size()); }

TokenIssuer::Issued TokenIssuer::issue(std::int64_t user_id, const std::string& handle, const std::string& name) const {
  return issue_at(user_id, handle, name, unix_now());
}

TokenIssuer::Issued TokenIssuer::issue_at(std::int64_t user_id, const std::string& handle, const std::string& name,
                                          std::int64_t now) const {
  if (user_id <= 0) {
    throw std::invalid_argument("user id must be positive");
  }
  const std::int64_t expires_at = now + ttl_.count();
  nlohmann::ordered_json header;
  header["alg"] = kAlgorithm;
  header["typ"] = "JWT";
  nlohmann::ordered_json payload;
  payload["iss"] = issuer_;
  payload["sub"] = std::to_string(user_id);
  payload["handle"] = handle;
  payload["name"] = name;
  payload["iat"] = now;
  payload["exp"] = expires_at;
  payload["jti"] = random_token_b64url(16);

  std::string signing_input = base64url_encode(header.dump()) + "." +
                              base64url_encode(payload.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace));
  std::array<unsigned char, crypto_sign_BYTES> signature{};
  crypto_sign_detached(signature.data(), nullptr, reinterpret_cast<const unsigned char*>(signing_input.data()),
                       signing_input.size(), secret_key_.data());
  signing_input += '.';
  signing_input += base64url_encode(signature);
  return {std::move(signing_input), expires_at};
}

std::string TokenIssuer::public_key_b64() const { return base64_encode(public_key_); }

struct TokenVerifier::Verified {
  Claims claims;
  std::int64_t not_before = 0;  ///< max(iat, nbf): the token must not be used earlier (with skew)
};

class TokenVerifier::Cache {
 public:
  explicit Cache(std::size_t entries) : lru(entries) {}
  LruCache<std::string, std::shared_ptr<const Verified>> lru;
};

TokenVerifier::TokenVerifier(std::string public_key_b64, std::string issuer, std::size_t cache_entries)
    : issuer_(std::move(issuer)), cache_(cache_entries > 0 ? std::make_shared<Cache>(cache_entries) : nullptr) {
  detail::ensure_sodium();
  const auto key = decode_exact(public_key_b64, crypto_sign_PUBLICKEYBYTES, "public key");
  std::copy(key.begin(), key.end(), public_key_.begin());
}

Claims TokenVerifier::verify(std::string_view token) const { return verify_at(token, unix_now()); }

Claims TokenVerifier::verify_at(std::string_view token, std::int64_t now) const {
  if (token.empty() || token.size() > kMaxTokenBytes) {
    throw invalid_token();
  }
  if (!cache_) {
    return check_time(verify_signature_and_claims(token), now);
  }
  std::string key(token);
  if (auto hit = cache_->lru.get(key)) {
    return check_time(**hit, now);
  }
  auto verified = std::make_shared<const Verified>(verify_signature_and_claims(token));
  auto claims = check_time(*verified, now);  // only cache tokens that are valid right now
  cache_->lru.put(std::move(key), std::move(verified));
  return claims;
}

Claims TokenVerifier::check_time(const Verified& verified, std::int64_t now) {
  if (verified.not_before > now + kClockSkewSeconds) {
    throw invalid_token();  // issued in the future, or not valid yet
  }
  if (now > verified.claims.expires_at + kClockSkewSeconds) {
    throw unauthenticated("Access token expired");
  }
  return verified.claims;
}

TokenVerifier::Verified TokenVerifier::verify_signature_and_claims(std::string_view token) const {
  const auto first_dot = token.find('.');
  if (first_dot == std::string_view::npos) {
    throw invalid_token();
  }
  const auto second_dot = token.find('.', first_dot + 1);
  if (second_dot == std::string_view::npos || token.find('.', second_dot + 1) != std::string_view::npos) {
    throw invalid_token();
  }
  const std::string_view header_b64 = token.substr(0, first_dot);
  const std::string_view payload_b64 = token.substr(first_dot + 1, second_dot - first_dot - 1);
  const std::string_view signature_b64 = token.substr(second_dot + 1);

  // 1. Header: only EdDSA is accepted (blocks "none", HS256-with-public-key and other confusion).
  const nlohmann::json header = parse_object(decode_segment(header_b64));
  const auto alg = header.find("alg");
  if (alg == header.end() || !alg->is_string() || alg->get<std::string>() != kAlgorithm) {
    throw invalid_token();
  }
  if (const auto typ = header.find("typ"); typ != header.end() && (!typ->is_string() || typ->get<std::string>() != "JWT")) {
    throw invalid_token();
  }
  if (header.contains("crit")) {  // RFC 7515 §4.1.11: unknown critical extensions must be rejected
    throw invalid_token();
  }

  // 2. Signature over the exact received "header.payload" bytes.
  const auto signature = decode_segment(signature_b64);
  if (signature.size() != crypto_sign_BYTES) {
    throw invalid_token();
  }
  const std::string_view signing_input = token.substr(0, second_dot);
  if (crypto_sign_verify_detached(signature.data(), reinterpret_cast<const unsigned char*>(signing_input.data()),
                                  signing_input.size(), public_key_.data()) != 0) {
    throw invalid_token();
  }

  // 3. Claims (only trusted after the signature check).
  const nlohmann::json payload = parse_object(decode_segment(payload_b64));
  if (string_claim(payload, "iss") != issuer_) {
    throw invalid_token();
  }
  Claims claims;
  claims.user_id = parse_subject(string_claim(payload, "sub"));
  claims.handle = string_claim(payload, "handle");
  claims.name = string_claim(payload, "name");
  claims.issued_at = integer_claim(payload, "iat");
  claims.expires_at = integer_claim(payload, "exp");
  if (claims.expires_at <= claims.issued_at) {
    throw invalid_token();
  }
  std::int64_t not_before = claims.issued_at;
  if (payload.contains("nbf")) {
    not_before = std::max(not_before, integer_claim(payload, "nbf"));
  }
  return {std::move(claims), not_before};
}

Claims require_auth(const httplib::Request& req, const TokenVerifier& verifier) {
  if (!req.has_header("Authorization")) {
    throw unauthenticated();
  }
  const std::string header = req.get_header_value("Authorization");
  constexpr std::string_view kScheme = "bearer ";
  if (header.size() <= kScheme.size()) {
    throw unauthenticated();
  }
  for (std::size_t i = 0; i < kScheme.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(header[i])) != kScheme[i]) {
      throw unauthenticated();
    }
  }
  std::string_view token(header);
  token.remove_prefix(kScheme.size());
  while (!token.empty() && token.front() == ' ') {
    token.remove_prefix(1);
  }
  while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
    token.remove_suffix(1);
  }
  return verifier.verify(token);
}

}  // namespace sg::common
