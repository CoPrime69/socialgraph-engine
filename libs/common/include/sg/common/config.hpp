#pragma once

/// @file config.hpp
/// Typed access to environment variables (12-factor configuration).
///
/// An environment variable that is set to the empty string is treated exactly like an unset one,
/// so `VAR=` in a Compose file falls back to the default.

#include <optional>
#include <string>
#include <string_view>

namespace sg::common {

/// Value of `name`, or std::nullopt when unset or empty.
[[nodiscard]] std::optional<std::string> env_get(std::string_view name);

/// Value of `name`, or `fallback` when unset or empty.
[[nodiscard]] std::string env_or(std::string_view name, std::string_view fallback);

/// Value of `name`. @throws std::runtime_error naming the variable when it is unset or empty.
[[nodiscard]] std::string env_required(std::string_view name);

/// Base-10 integer value of `name` in [min, max], or `fallback` when unset or empty.
/// @throws std::runtime_error naming the variable when the value is not an integer or out of range.
[[nodiscard]] long long env_int(std::string_view name, long long fallback, long long min, long long max);

/// Boolean value of `name` (true/false, 1/0, yes/no, on/off; case-insensitive), or `fallback` when
/// unset or empty. @throws std::runtime_error naming the variable on any other value.
[[nodiscard]] bool env_bool(std::string_view name, bool fallback);

}  // namespace sg::common
