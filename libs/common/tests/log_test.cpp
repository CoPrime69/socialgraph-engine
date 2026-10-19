#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "sg/common/log.hpp"

namespace {

using namespace sg::common;

class LogTest : public ::testing::Test {
 protected:
  std::vector<std::string> lines;
  void SetUp() override {
    init_logging("test-service", LogLevel::info);
    set_log_sink([this](std::string_view line) { lines.emplace_back(line); });
  }
  void TearDown() override {
    set_log_sink({});
    set_log_level(LogLevel::info);
  }
};

TEST_F(LogTest, WritesOneJsonObjectPerLine) {
  log_info("hello", {{"user_id", 7}, {"ok", true}});
  ASSERT_EQ(lines.size(), 1U);
  const auto line = nlohmann::json::parse(lines[0]);
  EXPECT_EQ(line["level"], "info");
  EXPECT_EQ(line["service"], "test-service");
  EXPECT_EQ(line["msg"], "hello");
  EXPECT_EQ(line["user_id"], 7);
  EXPECT_EQ(line["ok"], true);
  const std::string ts = line["ts"];
  EXPECT_EQ(ts.size(), 24U) << ts;  // 2026-10-19T20:00:00.123Z
  EXPECT_EQ(ts.back(), 'Z');
  EXPECT_EQ(lines[0].find('\n'), std::string::npos);
  EXPECT_EQ(lines[0].rfind("{\"ts\":", 0), 0U) << lines[0];
}

TEST_F(LogTest, FiltersByLevel) {
  log_debug("hidden");
  log_warn("shown");
  set_log_level(LogLevel::debug);
  log_debug("now shown");
  set_log_level(LogLevel::error);
  log_warn("hidden again");
  log_error("error");
  ASSERT_EQ(lines.size(), 3U);
  EXPECT_EQ(nlohmann::json::parse(lines[0])["msg"], "shown");
  EXPECT_EQ(nlohmann::json::parse(lines[1])["msg"], "now shown");
  EXPECT_EQ(nlohmann::json::parse(lines[2])["level"], "error");
}

TEST_F(LogTest, ReservedKeysAreNotOverwrittenAndBadUtf8IsReplaced) {
  log_info("msg", {{"level", "forged"}, {"path", std::string("/a\xff")}});
  ASSERT_EQ(lines.size(), 1U);
  const auto line = nlohmann::json::parse(lines[0]);
  EXPECT_EQ(line["level"], "info");
  EXPECT_EQ(line["field.level"], "forged");
  EXPECT_EQ(line["path"], "/a\xEF\xBF\xBD");
}

TEST(LogLevelParsing, AcceptsKnownNames) {
  EXPECT_EQ(parse_log_level("DEBUG"), LogLevel::debug);
  EXPECT_EQ(parse_log_level("info"), LogLevel::info);
  EXPECT_EQ(parse_log_level("warning"), LogLevel::warn);
  EXPECT_EQ(parse_log_level("error"), LogLevel::error);
  EXPECT_THROW((void)parse_log_level("loud"), std::invalid_argument);
}

}  // namespace
