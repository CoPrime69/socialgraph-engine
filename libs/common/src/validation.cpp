#include "sg/common/validation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sg::common {
namespace {

/// Decodes one UTF-8 sequence starting at text[i]; returns the code point and advances i, or
/// returns -1 for an invalid sequence.
long decode_utf8(std::string_view text, std::size_t& i) noexcept {
  const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(text[k]); };
  const unsigned char b0 = byte(i);
  if (b0 < 0x80) {
    ++i;
    return b0;
  }
  std::size_t extra = 0;
  long cp = 0;
  long min = 0;
  if ((b0 & 0xE0U) == 0xC0U) {
    extra = 1;
    cp = b0 & 0x1FU;
    min = 0x80;
  } else if ((b0 & 0xF0U) == 0xE0U) {
    extra = 2;
    cp = b0 & 0x0FU;
    min = 0x800;
  } else if ((b0 & 0xF8U) == 0xF0U) {
    extra = 3;
    cp = b0 & 0x07U;
    min = 0x10000;
  } else {
    return -1;
  }
  if (i + extra >= text.size()) {
    return -1;  // truncated sequence
  }
  for (std::size_t k = 1; k <= extra; ++k) {
    const unsigned char b = byte(i + k);
    if ((b & 0xC0U) != 0x80U) {
      return -1;
    }
    cp = (cp << 6U) | static_cast<long>(b & 0x3FU);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    return -1;
  }
  i += extra + 1;
  return cp;
}

bool is_ascii_space(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

}  // namespace

std::string normalize_handle(std::string_view handle) {
  std::string out(handle);
  std::transform(out.begin(), out.end(), out.begin(), [](char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  });
  return out;
}

bool is_valid_handle(std::string_view handle) noexcept {
  if (handle.size() < kHandleMinLength || handle.size() > kHandleMaxLength) {
    return false;
  }
  return std::all_of(handle.begin(), handle.end(),
                     [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

std::optional<std::string> normalize_text(std::string_view text, std::size_t max_code_points, bool allow_newlines) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && is_ascii_space(text[begin])) {
    ++begin;
  }
  while (end > begin && is_ascii_space(text[end - 1])) {
    --end;
  }
  const std::string_view trimmed = text.substr(begin, end - begin);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  std::size_t count = 0;
  for (std::size_t i = 0; i < trimmed.size();) {
    const long cp = decode_utf8(trimmed, i);
    if (cp < 0) {
      return std::nullopt;
    }
    const bool newline_like = cp == '\n' || cp == '\r' || cp == '\t';
    const bool control = cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F);
    if (control && !(allow_newlines && newline_like)) {
      return std::nullopt;
    }
    if (++count > max_code_points) {
      return std::nullopt;
    }
  }
  return std::string(trimmed);
}

std::optional<std::string> normalize_name(std::string_view name) { return normalize_text(name, kNameMaxLength); }

bool is_valid_password(std::string_view password) noexcept {
  return password.size() >= kPasswordMinBytes && password.size() <= kPasswordMaxBytes;
}

bool is_valid_influence(double influence) noexcept {
  return std::isfinite(influence) && influence >= kInfluenceMin && influence <= kInfluenceMax;
}

double round_influence(double influence) noexcept { return std::round(influence * 100.0) / 100.0; }

double round3(double value) noexcept { return std::round(value * 1000.0) / 1000.0; }

bool is_valid_utf8(std::string_view text) noexcept {
  for (std::size_t i = 0; i < text.size();) {
    if (decode_utf8(text, i) < 0) {
      return false;
    }
  }
  return true;
}

std::size_t utf8_length(std::string_view text) noexcept {
  std::size_t count = 0;
  for (std::size_t i = 0; i < text.size(); ++count) {
    if (decode_utf8(text, i) < 0) {
      ++i;  // count an invalid byte as one unit and continue
    }
  }
  return count;
}

}  // namespace sg::common
