#include <string>

#include <gtest/gtest.h>

#include "sg/common/validation.hpp"

namespace {

using namespace sg::common;

TEST(Validation, Handles) {
  EXPECT_TRUE(is_valid_handle("ada_l"));
  EXPECT_TRUE(is_valid_handle("abc"));
  EXPECT_TRUE(is_valid_handle(std::string(32, 'a')));
  EXPECT_FALSE(is_valid_handle("ab"));
  EXPECT_FALSE(is_valid_handle(std::string(33, 'a')));
  EXPECT_FALSE(is_valid_handle("Ada"));
  EXPECT_FALSE(is_valid_handle("ada-l"));
  EXPECT_FALSE(is_valid_handle("ada l"));
  EXPECT_FALSE(is_valid_handle("adà"));
  EXPECT_EQ(normalize_handle("Ada_L"), "ada_l");
}

TEST(Validation, NamesAreTrimmedAndCountedInCodePoints) {
  EXPECT_EQ(normalize_name("  Ada Lovelace \t"), "Ada Lovelace");
  EXPECT_FALSE(normalize_name(""));
  EXPECT_FALSE(normalize_name("   "));
  EXPECT_FALSE(normalize_name("Ada\x01"));
  EXPECT_FALSE(normalize_name("Ada\nLovelace"));
  EXPECT_FALSE(normalize_name("Ada\x7f"));
  EXPECT_FALSE(normalize_name("Ada\xC2\x85"));  // U+0085 (C1 control)
  EXPECT_FALSE(normalize_name("bad \xff utf8"));
  EXPECT_FALSE(normalize_name("\xC0\xAF"));      // overlong
  EXPECT_FALSE(normalize_name("\xED\xA0\x80"));  // surrogate
  std::string sixty_four_e_acute;
  for (int i = 0; i < 64; ++i) {
    sixty_four_e_acute += "\xC3\xA9";  // é: 2 bytes, 1 code point
  }
  EXPECT_TRUE(normalize_name(sixty_four_e_acute));
  EXPECT_FALSE(normalize_name(sixty_four_e_acute + "e"));
  EXPECT_TRUE(normalize_name("名前 😀"));
  EXPECT_EQ(utf8_length("名前 😀"), 4U);
}

TEST(Validation, FreeTextMayAllowNewlines) {
  EXPECT_EQ(normalize_text("line one\nline two", 500, true), "line one\nline two");
  EXPECT_FALSE(normalize_text("line one\nline two", 500, false));
  EXPECT_FALSE(normalize_text("bell\x07", 500, true));
  EXPECT_FALSE(normalize_text(std::string(501, 'a'), 500, true));
}

TEST(Validation, PasswordsInfluenceAndRounding) {
  EXPECT_FALSE(is_valid_password("1234567"));
  EXPECT_TRUE(is_valid_password("12345678"));
  EXPECT_TRUE(is_valid_password(std::string(128, 'x')));
  EXPECT_FALSE(is_valid_password(std::string(129, 'x')));
  EXPECT_TRUE(is_valid_influence(0.0));
  EXPECT_TRUE(is_valid_influence(100.0));
  EXPECT_FALSE(is_valid_influence(-0.01));
  EXPECT_FALSE(is_valid_influence(100.01));
  EXPECT_FALSE(is_valid_influence(std::numeric_limits<double>::quiet_NaN()));
  EXPECT_DOUBLE_EQ(round_influence(72.345), 72.35);
  EXPECT_DOUBLE_EQ(round3(0.12345), 0.123);
  EXPECT_DOUBLE_EQ(round3(0.9995), 1.0);
}

}  // namespace
