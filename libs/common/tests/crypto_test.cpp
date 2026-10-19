#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "sg/common/crypto.hpp"

namespace {

using namespace sg::common;

TEST(PasswordHash, HashAndVerify) {
  const auto params = PasswordHashParams::fast_for_tests();
  const std::string hash = hash_password("correct horse battery", params);
  EXPECT_EQ(hash.rfind("$argon2id$", 0), 0U) << hash;
  EXPECT_TRUE(verify_password(hash, "correct horse battery"));
  EXPECT_FALSE(verify_password(hash, "correct horse batterY"));
  EXPECT_FALSE(verify_password(hash, ""));
}

TEST(PasswordHash, SaltsAreRandom) {
  const auto params = PasswordHashParams::fast_for_tests();
  EXPECT_NE(hash_password("same password", params), hash_password("same password", params));
}

TEST(PasswordHash, MalformedHashNeverVerifies) {
  EXPECT_FALSE(verify_password("", "password"));
  EXPECT_FALSE(verify_password("not a hash", "password"));
  EXPECT_FALSE(verify_password(std::string(500, 'a'), "password"));
  EXPECT_FALSE(verify_password("$argon2id$v=19$m=8,t=1,p=1$c2FsdA$aGFzaA", "password"));
}

TEST(PasswordHash, OwaspParametersMatchTheCheatSheet) {
  const auto params = PasswordHashParams::owasp();
  EXPECT_EQ(params.opslimit, 2U);
  EXPECT_EQ(params.memlimit, 19U * 1024U * 1024U);
  const std::string hash = hash_password("an owasp password", params);
  EXPECT_NE(hash.find("m=19456,t=2,p=1"), std::string::npos) << hash;
  EXPECT_TRUE(verify_password(hash, "an owasp password"));
  EXPECT_FALSE(password_needs_rehash(hash, params));
  EXPECT_TRUE(password_needs_rehash(hash_password("x", PasswordHashParams::fast_for_tests()), params));
}

TEST(Base64Url, RoundTripsAllLengths) {
  for (std::size_t n = 0; n <= 70; ++n) {
    std::vector<unsigned char> data(n);
    for (std::size_t i = 0; i < n; ++i) {
      data[i] = static_cast<unsigned char>((i * 37 + n) & 0xFFU);
    }
    const std::string encoded = base64url_encode(data);
    EXPECT_EQ(encoded.find_first_of("+/="), std::string::npos) << encoded;
    EXPECT_EQ(base64url_decode(encoded), data) << n;
  }
}

TEST(Base64Url, KnownVectors) {
  EXPECT_EQ(base64url_encode(std::string_view("")), "");
  EXPECT_EQ(base64url_encode(std::string_view("f")), "Zg");
  EXPECT_EQ(base64url_encode(std::string_view("fo")), "Zm8");
  EXPECT_EQ(base64url_encode(std::string_view("foo")), "Zm9v");
  EXPECT_EQ(base64url_encode(std::string_view("\xfb\xff")), "-_8");
}

TEST(Base64Url, StrictDecoderRejectsGarbage) {
  EXPECT_THROW((void)base64url_decode("Zg=="), std::invalid_argument);  // padding
  EXPECT_THROW((void)base64url_decode("Zm9v!"), std::invalid_argument);
  EXPECT_THROW((void)base64url_decode("+/8"), std::invalid_argument);   // standard alphabet
  EXPECT_THROW((void)base64url_decode("Z"), std::invalid_argument);     // impossible length
  EXPECT_THROW((void)base64url_decode("Zh"), std::invalid_argument);    // non-canonical trailing bits
  EXPECT_THROW((void)base64url_decode("Zm 9v"), std::invalid_argument);
}

TEST(Base64, StandardVariantAndKeyDecoding) {
  const std::vector<unsigned char> data{0xfb, 0xff, 0x00, 0x10};
  const std::string encoded = base64_encode(data);
  EXPECT_EQ(encoded, "+/8AEA==");
  EXPECT_EQ(base64_decode(encoded), data);
  EXPECT_EQ(base64_decode("+/8AEA"), data);
  EXPECT_EQ(decode_key_b64("  +/8AEA==\n"), data);
  EXPECT_EQ(decode_key_b64("-_8AEA"), data);
  EXPECT_THROW((void)base64_decode("***"), std::invalid_argument);
  EXPECT_THROW((void)decode_key_b64(""), std::invalid_argument);
}

TEST(RandomTokens, LengthAndUniqueness) {
  std::set<std::string> seen;
  for (int i = 0; i < 100; ++i) {
    const std::string token = random_token_b64url(32);
    EXPECT_EQ(token.size(), 43U);
    EXPECT_TRUE(seen.insert(token).second);
  }
  EXPECT_EQ(random_hex(16).size(), 32U);
}

TEST(ConstantTimeEquals, ComparesContent) {
  EXPECT_TRUE(constant_time_equals("abc", "abc"));
  EXPECT_TRUE(constant_time_equals("", ""));
  EXPECT_FALSE(constant_time_equals("abc", "abd"));
  EXPECT_FALSE(constant_time_equals("abc", "abcd"));
}

}  // namespace
