#include "sg/common/time.hpp"

#include <array>
#include <cstdio>
#include <ctime>

namespace sg::common {
namespace {

std::tm utc_tm(std::time_t seconds) {
  std::tm tm{};
  gmtime_r(&seconds, &tm);
  return tm;
}

std::string format_seconds(std::time_t seconds) {
  const std::tm tm = utc_tm(seconds);
  std::array<char, 32> buffer{};
  const std::size_t n = std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buffer.data(), n);
}

}  // namespace

std::string to_rfc3339(std::chrono::system_clock::time_point tp) {
  const auto seconds = std::chrono::floor<std::chrono::seconds>(tp);
  return format_seconds(static_cast<std::time_t>(seconds.time_since_epoch().count()));
}

std::string to_rfc3339_millis(std::chrono::system_clock::time_point tp) {
  const auto seconds = std::chrono::floor<std::chrono::seconds>(tp);
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(tp - seconds).count();
  const std::tm tm = utc_tm(static_cast<std::time_t>(seconds.time_since_epoch().count()));
  std::array<char, 40> buffer{};
  const int n = std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900,
                              tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(millis));
  return std::string(buffer.data(), n > 0 ? static_cast<std::size_t>(n) : 0);
}

std::string unix_to_rfc3339(std::int64_t unix_seconds) {
  return format_seconds(static_cast<std::time_t>(unix_seconds));
}

std::int64_t unix_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace sg::common
