#include <cstdlib>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "sg/common/config.hpp"

namespace {

using sg::common::env_bool;
using sg::common::env_int;
using sg::common::env_or;
using sg::common::env_required;

/// Sets an environment variable for the lifetime of the guard.
class EnvGuard {
 public:
  EnvGuard(const char* name, const char* value) : name_(name) {
    if (value == nullptr) {
      ::unsetenv(name);
    } else {
      ::setenv(name, value, 1);
    }
  }
  ~EnvGuard() { ::unsetenv(name_.c_str()); }
  EnvGuard(const EnvGuard&) = delete;
  EnvGuard& operator=(const EnvGuard&) = delete;

 private:
  std::string name_;
};

TEST(Config, EnvOrUsesFallbackWhenUnsetOrEmpty) {
  {
    EnvGuard g("SG_TEST_CFG", nullptr);
    EXPECT_EQ(env_or("SG_TEST_CFG", "fallback"), "fallback");
  }
  {
    EnvGuard g("SG_TEST_CFG", "");
    EXPECT_EQ(env_or("SG_TEST_CFG", "fallback"), "fallback");
  }
  {
    EnvGuard g("SG_TEST_CFG", "value");
    EXPECT_EQ(env_or("SG_TEST_CFG", "fallback"), "value");
  }
}

TEST(Config, EnvRequiredNamesTheVariable) {
  EnvGuard g("SG_TEST_REQUIRED", nullptr);
  try {
    (void)env_required("SG_TEST_REQUIRED");
    FAIL() << "expected an exception";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("SG_TEST_REQUIRED"), std::string::npos);
  }
  EnvGuard set("SG_TEST_REQUIRED", "x");
  EXPECT_EQ(env_required("SG_TEST_REQUIRED"), "x");
}

TEST(Config, EnvIntParsesAndValidatesRange) {
  {
    EnvGuard g("SG_TEST_INT", nullptr);
    EXPECT_EQ(env_int("SG_TEST_INT", 42, 1, 100), 42);
  }
  {
    EnvGuard g("SG_TEST_INT", "17");
    EXPECT_EQ(env_int("SG_TEST_INT", 42, 1, 100), 17);
  }
  {
    EnvGuard g("SG_TEST_INT", "+17");
    EXPECT_EQ(env_int("SG_TEST_INT", 42, 1, 100), 17);
  }
  {
    EnvGuard g("SG_TEST_INT", "-5");
    EXPECT_EQ(env_int("SG_TEST_INT", 0, -10, 10), -5);
  }
  for (const char* bad : {"abc", "12abc", "101", "0", " 5", "5 ", "1e3", "99999999999999999999999"}) {
    EnvGuard g("SG_TEST_INT", bad);
    EXPECT_THROW((void)env_int("SG_TEST_INT", 42, 1, 100), std::runtime_error) << bad;
  }
}

TEST(Config, EnvBoolAcceptsCommonSpellings) {
  for (const char* yes : {"true", "TRUE", "1", "yes", "Yes", "on"}) {
    EnvGuard g("SG_TEST_BOOL", yes);
    EXPECT_TRUE(env_bool("SG_TEST_BOOL", false)) << yes;
  }
  for (const char* no : {"false", "False", "0", "no", "off"}) {
    EnvGuard g("SG_TEST_BOOL", no);
    EXPECT_FALSE(env_bool("SG_TEST_BOOL", true)) << no;
  }
  {
    EnvGuard g("SG_TEST_BOOL", nullptr);
    EXPECT_TRUE(env_bool("SG_TEST_BOOL", true));
  }
  EnvGuard g("SG_TEST_BOOL", "maybe");
  EXPECT_THROW((void)env_bool("SG_TEST_BOOL", true), std::runtime_error);
}

}  // namespace
