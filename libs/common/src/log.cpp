#include "sg/common/log.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "sg/common/time.hpp"

namespace sg::common {
namespace {

struct LogState {
  std::mutex mutex;
  std::string service = "socialgraph";
  std::function<void(std::string_view)> sink;
};

LogState& state() {
  static LogState instance;
  return instance;
}

std::atomic<int> g_level{static_cast<int>(LogLevel::info)};

constexpr std::array<std::string_view, 4> kReservedKeys{"ts", "level", "service", "msg"};

bool is_reserved(const std::string& key) {
  return std::find(kReservedKeys.begin(), kReservedKeys.end(), key) != kReservedKeys.end();
}

}  // namespace

LogLevel parse_log_level(std::string_view text) {
  std::string lower(text);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "debug" || lower == "trace") {
    return LogLevel::debug;
  }
  if (lower == "info") {
    return LogLevel::info;
  }
  if (lower == "warn" || lower == "warning") {
    return LogLevel::warn;
  }
  if (lower == "error") {
    return LogLevel::error;
  }
  throw std::invalid_argument("unknown log level '" + std::string(text) + "' (expected debug, info, warn or error)");
}

std::string_view to_string(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::debug:
      return "debug";
    case LogLevel::info:
      return "info";
    case LogLevel::warn:
      return "warn";
    case LogLevel::error:
      return "error";
  }
  return "info";
}

void init_logging(std::string service, LogLevel level) {
  {
    std::lock_guard lock(state().mutex);
    state().service = std::move(service);
  }
  set_log_level(level);
}

void set_log_level(LogLevel level) noexcept { g_level.store(static_cast<int>(level)); }

bool log_enabled(LogLevel level) noexcept { return static_cast<int>(level) >= g_level.load(); }

void set_log_sink(std::function<void(std::string_view line)> sink) {
  std::lock_guard lock(state().mutex);
  state().sink = std::move(sink);
}

void log(LogLevel level, std::string_view msg, const nlohmann::json& fields) {
  if (!log_enabled(level)) {
    return;
  }
  try {
    nlohmann::ordered_json line;
    line["ts"] = to_rfc3339_millis(std::chrono::system_clock::now());
    line["level"] = to_string(level);
    {
      std::lock_guard lock(state().mutex);
      line["service"] = state().service;
    }
    line["msg"] = msg;
    if (fields.is_object()) {
      for (const auto& [key, value] : fields.items()) {
        line[is_reserved(key) ? "field." + key : key] = value;
      }
    } else if (!fields.is_null()) {
      line["data"] = fields;
    }
    // Invalid UTF-8 (e.g. from a request path) is replaced instead of throwing.
    std::string text = line.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);

    std::lock_guard lock(state().mutex);
    if (state().sink) {
      state().sink(text);
    } else {
      text.push_back('\n');
      std::fwrite(text.data(), 1, text.size(), stdout);
      std::fflush(stdout);
    }
  } catch (...) {
    // Logging must never take a request or a background thread down.
  }
}

void log_debug(std::string_view msg, const nlohmann::json& fields) { log(LogLevel::debug, msg, fields); }
void log_info(std::string_view msg, const nlohmann::json& fields) { log(LogLevel::info, msg, fields); }
void log_warn(std::string_view msg, const nlohmann::json& fields) { log(LogLevel::warn, msg, fields); }
void log_error(std::string_view msg, const nlohmann::json& fields) { log(LogLevel::error, msg, fields); }

}  // namespace sg::common
