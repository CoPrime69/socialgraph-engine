#pragma once

/// @file validation.hpp
/// Field rules shared by the services (contracts.md §3 and §10). The category/branch taxonomy is
/// data-driven (graph-service tables), so it is not part of this header.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace sg::common {

inline constexpr std::size_t kHandleMinLength = 3;
inline constexpr std::size_t kHandleMaxLength = 32;
inline constexpr std::size_t kNameMaxLength = 64;  ///< Unicode code points after trimming
inline constexpr std::size_t kPasswordMinBytes = 8;
inline constexpr std::size_t kPasswordMaxBytes = 128;
inline constexpr double kInfluenceMin = 0.0;
inline constexpr double kInfluenceMax = 100.0;

/// Messages used in validation_failed details.
inline constexpr std::string_view kHandleRule = "must be 3-32 characters of a-z, 0-9 or _";
inline constexpr std::string_view kNameRule = "must be 1-64 characters without control characters";
inline constexpr std::string_view kPasswordRule = "must be 8-128 bytes";

/// ASCII lower-case copy (handles are stored lower-case).
[[nodiscard]] std::string normalize_handle(std::string_view handle);
/// `^[a-z0-9_]{3,32}$`.
[[nodiscard]] bool is_valid_handle(std::string_view handle) noexcept;

/// Trims ASCII whitespace; returns the trimmed text when it is valid UTF-8 of 1..max_code_points
/// code points without control characters (C0, DEL, C1; `allow_newlines` permits \n, \r and \t),
/// else std::nullopt.
[[nodiscard]] std::optional<std::string> normalize_text(std::string_view text, std::size_t max_code_points,
                                                        bool allow_newlines = false);

/// normalize_text(name, 64): display names.
[[nodiscard]] std::optional<std::string> normalize_name(std::string_view name);

/// 8-128 bytes.
[[nodiscard]] bool is_valid_password(std::string_view password) noexcept;

/// Finite and within [0, 100].
[[nodiscard]] bool is_valid_influence(double influence) noexcept;
/// Rounded to 2 decimals (half away from zero), as stored.
[[nodiscard]] double round_influence(double influence) noexcept;
/// Rounded to 3 decimals (half away from zero), the precision of connection strengths.
[[nodiscard]] double round3(double value) noexcept;

/// True for well-formed UTF-8 (no overlongs, surrogates or code points above U+10FFFF).
[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept;
/// Number of code points of valid UTF-8 text.
[[nodiscard]] std::size_t utf8_length(std::string_view text) noexcept;

}  // namespace sg::common
