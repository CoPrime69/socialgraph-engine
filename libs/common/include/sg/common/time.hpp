#pragma once

/// @file time.hpp
/// RFC 3339 UTC timestamps and Unix time.

#include <chrono>
#include <cstdint>
#include <string>

namespace sg::common {

/// "2026-10-19T20:00:00Z" (UTC, whole seconds; sub-second precision is truncated).
[[nodiscard]] std::string to_rfc3339(std::chrono::system_clock::time_point tp);

/// "2026-10-19T20:00:00.123Z" (UTC, milliseconds).
[[nodiscard]] std::string to_rfc3339_millis(std::chrono::system_clock::time_point tp);

/// to_rfc3339() of a Unix timestamp in seconds.
[[nodiscard]] std::string unix_to_rfc3339(std::int64_t unix_seconds);

/// Current Unix time in seconds.
[[nodiscard]] std::int64_t unix_now();

}  // namespace sg::common
