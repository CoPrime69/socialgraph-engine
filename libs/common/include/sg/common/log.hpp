#pragma once

/// @file log.hpp
/// Structured logging: one JSON object per line on stdout,
/// `{"ts","level","service","msg", ...fields}`. Thread-safe; lines are never interleaved.
/// Never pass secrets (passwords, tokens, Authorization headers) or request bodies as fields.

#include <functional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace sg::common {

enum class LogLevel { debug, info, warn, error };

/// Parses debug/info/warn/warning/error (case-insensitive). @throws std::invalid_argument.
[[nodiscard]] LogLevel parse_log_level(std::string_view text);

/// Lower-case name of a level ("debug", "info", "warn", "error").
[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;

/// Sets the service name written on every line and the minimum level. May be called again.
void init_logging(std::string service, LogLevel level);

/// Changes the minimum level at runtime.
void set_log_level(LogLevel level) noexcept;

/// True when a message at `level` would be written.
[[nodiscard]] bool log_enabled(LogLevel level) noexcept;

/// Redirects complete log lines (without the trailing newline) to `sink`, e.g. to capture them
/// in tests. Passing an empty function restores stdout.
void set_log_sink(std::function<void(std::string_view line)> sink);

/// Writes one line. Keys of `fields` (a JSON object) are added after the standard keys; a field
/// that collides with a standard key is written as "field.<key>". Never throws.
void log(LogLevel level, std::string_view msg, const nlohmann::json& fields = nlohmann::json::object());

void log_debug(std::string_view msg, const nlohmann::json& fields = nlohmann::json::object());
void log_info(std::string_view msg, const nlohmann::json& fields = nlohmann::json::object());
void log_warn(std::string_view msg, const nlohmann::json& fields = nlohmann::json::object());
void log_error(std::string_view msg, const nlohmann::json& fields = nlohmann::json::object());

}  // namespace sg::common
