#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sodium.h>

#include "sg/common/auth.hpp"
#include "sg/common/crypto.hpp"
#include "sg/common/errors.hpp"
#include "sg/common/time.hpp"

namespace {

using namespace sg::common;
using nlohmann::json;

struct Parts {
  std::string header;
  std::string payload;
  std::string signature;
};

Parts split(const std::string& token) {
  const auto a = token.find('.');
  const auto b = token.find('.', a + 1);
  return {token.substr(0, a), token.substr(a + 1, b - a - 1), token.substr(b + 1)};
}

std::string b64(const std::string& text) { return base64url_encode(std::string_view(text)); }

std::string decode_text(const std::string& segment) {
  const auto bytes = base64url_decode(segment);
  return {bytes.begin(), bytes.end()};
}

/// Signs header.payload with the given seed, exactly like the issuer does, so tests can forge
/// tokens with arbitrary headers/claims that carry a *valid* signature.
std::string sign_with(const std::string& seed_b64, const json& header, const json& payload) {
  const auto seed = decode_key_b64(seed_b64);
  unsigned char pk[crypto_sign_PUBLICKEYBYTES];
  unsigned char sk[crypto_sign_SECRETKEYBYTES];
  crypto_sign_seed_keypair(pk, sk, seed.data());
  const std::string input = b64(header.dump()) + "." + b64(payload.dump());
  unsigned char sig[crypto_sign_BYTES];
  crypto_sign_detached(sig, nullptr, reinterpret_cast<const unsigned char*>(input.data()), input.size(), sk);
  return input + "." + base64url_encode(std::span<const unsigned char>(sig, sizeof sig));
}

class TokenTest : public ::testing::Test {
 protected:
  KeyPair keys = generate_keypair();
  TokenIssuer issuer{keys.seed_b64, std::chrono::hours(8)};
  TokenVerifier verifier{keys.public_key_b64};

  json valid_payload(std::int64_t now) const {
    return {{"iss", "socialgraph-identity"}, {"sub", "101"}, {"handle", "ada_l"}, {"name", "Ada"},
            {"iat", now},                   {"exp", now + 3600}, {"jti", "x"}};
  }

  void expect_rejected(const std::string& token) const {
    try {
      (void)verifier.verify(token);
      ADD_FAILURE() << "token accepted: " << token;
    } catch (const ApiError& e) {
      EXPECT_EQ(e.status(), 401);
      EXPECT_EQ(e.code(), "unauthenticated");
    }
  }
};

TEST_F(TokenTest, IssueAndVerifyRoundTrip) {
  const auto issued = issuer.issue(101, "ada_l", "Ada Lovelace");
  const Claims claims = verifier.verify(issued.token);
  EXPECT_EQ(claims.user_id, 101);
  EXPECT_EQ(claims.handle, "ada_l");
  EXPECT_EQ(claims.name, "Ada Lovelace");
  EXPECT_EQ(claims.expires_at, issued.expires_at);
  EXPECT_EQ(claims.expires_at - claims.issued_at, 8 * 3600);
  EXPECT_NEAR(static_cast<double>(claims.issued_at), static_cast<double>(unix_now()), 2.0);
  EXPECT_EQ(issuer.public_key_b64(), keys.public_key_b64);
  EXPECT_EQ(public_key_from_seed(keys.seed_b64), keys.public_key_b64);
}

TEST_F(TokenTest, HeaderAndClaimsFollowTheContract) {
  const auto token = issuer.issue(7, "bob", "Bob").token;
  const Parts parts = split(token);
  EXPECT_EQ(json::parse(decode_text(parts.header)), json::parse(R"({"alg":"EdDSA","typ":"JWT"})"));
  const json payload = json::parse(decode_text(parts.payload));
  EXPECT_EQ(payload["iss"], "socialgraph-identity");
  EXPECT_EQ(payload["sub"], "7");
  EXPECT_TRUE(payload["jti"].is_string());
  EXPECT_GE(payload["jti"].get<std::string>().size(), 16U);
  EXPECT_NE(issuer.issue(7, "bob", "Bob").token, token);  // fresh jti
  EXPECT_EQ(token.find('='), std::string::npos);
}

TEST_F(TokenTest, TamperedPayloadIsRejected) {
  const auto token = issuer.issue(101, "ada_l", "Ada").token;
  Parts parts = split(token);
  json payload = json::parse(decode_text(parts.payload));
  payload["sub"] = "1";
  expect_rejected(parts.header + "." + b64(payload.dump()) + "." + parts.signature);
}

TEST_F(TokenTest, TamperedSignatureIsRejected) {
  const auto token = issuer.issue(101, "ada_l", "Ada").token;
  Parts parts = split(token);
  auto sig = base64url_decode(parts.signature);
  sig[0] ^= 0x01U;
  expect_rejected(parts.header + "." + parts.payload + "." + base64url_encode(sig));
  expect_rejected(parts.header + "." + parts.payload + "." + parts.signature.substr(0, 20));
}

TEST_F(TokenTest, TokenSignedWithAnotherKeyIsRejected) {
  const KeyPair other = generate_keypair();
  const TokenIssuer other_issuer(other.seed_b64, std::chrono::hours(1));
  expect_rejected(other_issuer.issue(101, "ada_l", "Ada").token);
}

TEST_F(TokenTest, ExpiredTokenIsRejectedOutsideTheSkew) {
  const std::int64_t now = unix_now();
  const auto old = issuer.issue_at(101, "ada_l", "Ada", now - 8 * 3600 - 31);  // expired 31 s ago
  try {
    (void)verifier.verify(old.token);
    FAIL() << "expired token accepted";
  } catch (const ApiError& e) {
    EXPECT_EQ(e.status(), 401);
    EXPECT_EQ(e.message(), "Access token expired");
  }
  const auto within_skew = issuer.issue_at(101, "ada_l", "Ada", now - 8 * 3600 - 10);  // expired 10 s ago
  EXPECT_NO_THROW((void)verifier.verify(within_skew.token));
}

TEST_F(TokenTest, CachedTokenIsStillCheckedForExpiry) {
  const std::int64_t now = unix_now();
  const auto token = issuer.issue_at(101, "ada_l", "Ada", now).token;
  EXPECT_EQ(verifier.verify_at(token, now).user_id, 101);  // verified and cached
  EXPECT_EQ(verifier.verify_at(token, now + 60).user_id, 101);  // served from the cache
  try {
    (void)verifier.verify_at(token, now + 8 * 3600 + 31);
    FAIL() << "expired token accepted from the cache";
  } catch (const ApiError& e) {
    EXPECT_EQ(e.message(), "Access token expired");
  }
  // Not usable before it was issued either (beyond the skew).
  EXPECT_THROW((void)verifier.verify_at(token, now - 31), ApiError);
}

TEST_F(TokenTest, ForgedTokenIsRejectedEvenAfterTheGenuineOneWasCached) {
  const auto genuine = issuer.issue(101, "ada_l", "Ada").token;
  (void)verifier.verify(genuine);
  auto parts = split(genuine);
  auto payload = json::parse(decode_text(parts.payload));
  payload["sub"] = "102";
  expect_rejected(parts.header + "." + b64(payload.dump()) + "." + parts.signature);
  expect_rejected(genuine.substr(0, genuine.size() - 2) + (genuine.ends_with("AA") ? "BB" : "AA"));
  EXPECT_EQ(verifier.verify(genuine).user_id, 101);
}

TEST_F(TokenTest, CacheCanBeDisabled) {
  const TokenVerifier uncached(keys.public_key_b64, "socialgraph-identity", 0);
  const auto token = issuer.issue(101, "ada_l", "Ada").token;
  EXPECT_EQ(uncached.verify(token).user_id, 101);
  EXPECT_EQ(uncached.verify(token).handle, "ada_l");
}

TEST_F(TokenTest, TokenFromTheFutureIsRejected) {
  expect_rejected(issuer.issue_at(101, "ada_l", "Ada", unix_now() + 3600).token);
}

TEST_F(TokenTest, AlgNoneIsRejected) {
  const std::int64_t now = unix_now();
  const std::string unsigned_token =
      b64(R"({"alg":"none","typ":"JWT"})") + "." + b64(valid_payload(now).dump()) + ".";
  expect_rejected(unsigned_token);
  // Even with a valid Ed25519 signature, any alg other than EdDSA is refused.
  expect_rejected(sign_with(keys.seed_b64, {{"alg", "none"}, {"typ", "JWT"}}, valid_payload(now)));
}

TEST_F(TokenTest, Hs256ConfusionIsRejected) {
  const std::int64_t now = unix_now();
  // Classic confusion: HMAC "signed" with the public key as the secret.
  const std::string input = b64(R"({"alg":"HS256","typ":"JWT"})") + "." + b64(valid_payload(now).dump());
  const auto pk = decode_key_b64(keys.public_key_b64);
  unsigned char mac[crypto_auth_hmacsha256_BYTES];
  crypto_auth_hmacsha256_state st;
  crypto_auth_hmacsha256_init(&st, pk.data(), pk.size());
  crypto_auth_hmacsha256_update(&st, reinterpret_cast<const unsigned char*>(input.data()), input.size());
  crypto_auth_hmacsha256_final(&st, mac);
  expect_rejected(input + "." + base64url_encode(std::span<const unsigned char>(mac, sizeof mac)));
  expect_rejected(sign_with(keys.seed_b64, {{"alg", "HS256"}}, valid_payload(now)));
  expect_rejected(sign_with(keys.seed_b64, {{"typ", "JWT"}}, valid_payload(now)));  // no alg
  expect_rejected(sign_with(keys.seed_b64, {{"alg", "EdDSA"}, {"crit", {"exp"}}}, valid_payload(now)));
}

TEST_F(TokenTest, ValidlySignedButBadClaimsAreRejected) {
  const std::int64_t now = unix_now();
  const json header = {{"alg", "EdDSA"}, {"typ", "JWT"}};
  EXPECT_NO_THROW((void)verifier.verify(sign_with(keys.seed_b64, header, valid_payload(now))));

  json wrong_issuer = valid_payload(now);
  wrong_issuer["iss"] = "someone-else";
  expect_rejected(sign_with(keys.seed_b64, header, wrong_issuer));

  for (const json& sub : {json("abc"), json("0"), json("-5"), json(101), json("1e3"), json("")}) {
    json payload = valid_payload(now);
    payload["sub"] = sub;
    expect_rejected(sign_with(keys.seed_b64, header, payload));
  }
  json no_exp = valid_payload(now);
  no_exp.erase("exp");
  expect_rejected(sign_with(keys.seed_b64, header, no_exp));
  json string_exp = valid_payload(now);
  string_exp["exp"] = std::to_string(now + 3600);
  expect_rejected(sign_with(keys.seed_b64, header, string_exp));
  json no_handle = valid_payload(now);
  no_handle.erase("handle");
  expect_rejected(sign_with(keys.seed_b64, header, no_handle));
  json not_before = valid_payload(now);
  not_before["nbf"] = now + 600;
  expect_rejected(sign_with(keys.seed_b64, header, not_before));
}

TEST_F(TokenTest, MalformedTokensAreRejected) {
  const auto token = issuer.issue(101, "ada_l", "Ada").token;
  const Parts parts = split(token);
  for (const std::string& bad : {std::string(), std::string("."), std::string(".."), std::string("abc"),
                                 parts.header + "." + parts.payload,
                                 parts.header + "." + parts.payload + "." + parts.signature + ".x",
                                 "." + parts.payload + "." + parts.signature, parts.header + ".." + parts.signature,
                                 parts.header + "=." + parts.payload + "." + parts.signature,
                                 b64("not json") + "." + parts.payload + "." + parts.signature,
                                 b64("[1,2]") + "." + parts.payload + "." + parts.signature,
                                 std::string(TokenVerifier::kMaxTokenBytes + 1, 'a')}) {
    expect_rejected(bad);
  }
}

TEST(TokenKeys, InvalidKeyMaterialIsRejectedAtConstruction) {
  EXPECT_THROW(TokenVerifier("not base64!"), std::invalid_argument);
  EXPECT_THROW(TokenVerifier(base64_encode(std::vector<unsigned char>(16, 1))), std::invalid_argument);
  EXPECT_THROW(TokenIssuer(base64_encode(std::vector<unsigned char>(31, 1)), std::chrono::hours(1)),
               std::invalid_argument);
  const KeyPair keys = generate_keypair();
  EXPECT_THROW(TokenIssuer(keys.seed_b64, std::chrono::seconds(0)), std::invalid_argument);
  EXPECT_NE(generate_keypair().seed_b64, keys.seed_b64);
}

TEST(RequireAuth, ParsesBearerHeader) {
  const KeyPair keys = generate_keypair();
  const TokenIssuer issuer(keys.seed_b64, std::chrono::hours(1));
  const TokenVerifier verifier(keys.public_key_b64);
  const std::string token = issuer.issue(5, "eve", "Eve").token;

  Request req;
  EXPECT_THROW((void)require_auth(req, verifier), ApiError);
  req.set_header("Authorization", "Bearer " + token);
  EXPECT_EQ(require_auth(req, verifier).user_id, 5);

  Request lower;
  lower.set_header("Authorization", "bearer " + token);
  EXPECT_EQ(require_auth(lower, verifier).user_id, 5);

  for (const std::string& value : {"Basic " + token, std::string("Bearer"), std::string("Bearer "), token,
                                   "Bearer " + token + "x"}) {
    Request bad;
    bad.set_header("Authorization", value);
    try {
      (void)require_auth(bad, verifier);
      ADD_FAILURE() << "accepted: " << value;
    } catch (const ApiError& e) {
      EXPECT_EQ(e.status(), 401);
    }
  }
}

}  // namespace
