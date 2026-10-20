#include "sg/common/config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <system_error>

namespace sg::common {

std::optional<std::string> env_get(std::string_view name) {
  const std::string key(name);
  const char* value = std::getenv(key.c_str());  // NOLINT(concurrency-mt-unsafe): read-only after start-up
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  return std::string(value);
}

std::string env_or(std::string_view name, std::string_view fallback) {
  auto value = env_get(name);
  return value ? std::move(*value) : std::string(fallback);
}

std::string env_required(std::string_view name) {
  auto value = env_get(name);
  if (!value) {
    throw std::runtime_error("required environment variable " + std::string(name) + " is not set");
  }
  return std::move(*value);
}

long long env_int(std::string_view name, long long fallback, long long min, long long max) {
  const auto value = env_get(name);
  if (!value) {
    return fallback;
  }
  const std::string& text = *value;
  long long parsed = 0;
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  if (begin != end && *begin == '+') {
    ++begin;
  }
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (begin == end || ec != std::errc() || ptr != end || parsed < min || parsed > max) {
    throw std::runtime_error("environment variable " + std::string(name) + " must be an integer between " +
                             std::to_string(min) + " and " + std::to_string(max) + " (got '" + text + "')");
  }
  return parsed;
}

bool env_bool(std::string_view name, bool fallback) {
  const auto value = env_get(name);
  if (!value) {
    return fallback;
  }
  std::string text = *value;
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (text == "true" || text == "1" || text == "yes" || text == "on") {
    return true;
  }
  if (text == "false" || text == "0" || text == "no" || text == "off") {
    return false;
  }
  throw std::runtime_error("environment variable " + std::string(name) +
                           " must be a boolean (true/false, 1/0, yes/no, on/off; got '" + *value + "')");
}

}  // namespace sg::common
