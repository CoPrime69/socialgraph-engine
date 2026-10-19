#include "sg/common/crypto.hpp"

#include <array>
#include <mutex>
#include <stdexcept>
#include <string>

#include <sodium.h>

#include "sodium_init.hpp"

namespace sg::common {
namespace detail {

void ensure_sodium() {
  static std::once_flag once;
  std::call_once(once, [] {
    if (sodium_init() < 0) {
      throw std::runtime_error("libsodium initialisation failed");
    }
  });
}

}  // namespace detail

namespace {

constexpr std::size_t kMaxPasswordBytes = 4096;

std::string encode(std::span<const unsigned char> data, int variant) {
  detail::ensure_sodium();
  const std::size_t encoded_len = sodium_base64_encoded_len(data.size(), variant);
  std::string out(encoded_len, '\0');
  sodium_bin2base64(out.data(), out.size(), data.data(), data.size(), variant);
  out.resize(encoded_len - 1);  // drop the terminating NUL
  return out;
}

bool try_decode(std::string_view text, int variant, std::vector<unsigned char>& out) {
  detail::ensure_sodium();
  out.assign(text.size() * 3 / 4 + 3, 0);
  std::size_t len = 0;
  if (sodium_base642bin(out.data(), out.size(), text.data(), text.size(), nullptr, &len, nullptr, variant) != 0) {
    out.clear();
    return false;
  }
  out.resize(len);
  return true;
}

}  // namespace

PasswordHashParams PasswordHashParams::owasp() {
  return {2ULL, static_cast<std::size_t>(19) * 1024U * 1024U};
}

PasswordHashParams PasswordHashParams::fast_for_tests() {
  return {crypto_pwhash_argon2id_OPSLIMIT_MIN, crypto_pwhash_argon2id_MEMLIMIT_MIN};
}

std::string hash_password(std::string_view password, const PasswordHashParams& params) {
  detail::ensure_sodium();
  if (password.size() > kMaxPasswordBytes) {
    throw std::invalid_argument("password too long");
  }
  std::array<char, crypto_pwhash_STRBYTES> out{};
  if (crypto_pwhash_str_alg(out.data(), password.data(), password.size(), params.opslimit, params.memlimit,
                            crypto_pwhash_ALG_ARGON2ID13) != 0) {
    throw std::runtime_error("password hashing failed (out of memory)");
  }
  return std::string(out.data());
}

bool verify_password(std::string_view stored_hash, std::string_view password) {
  detail::ensure_sodium();
  if (stored_hash.empty() || stored_hash.size() >= crypto_pwhash_STRBYTES || password.size() > kMaxPasswordBytes) {
    return false;
  }
  const std::string hash(stored_hash);  // NUL-terminated copy
  return crypto_pwhash_str_verify(hash.c_str(), password.data(), password.size()) == 0;
}

bool password_needs_rehash(std::string_view stored_hash, const PasswordHashParams& params) {
  detail::ensure_sodium();
  if (stored_hash.empty() || stored_hash.size() >= crypto_pwhash_STRBYTES) {
    return true;
  }
  const std::string hash(stored_hash);
  return crypto_pwhash_str_needs_rehash(hash.c_str(), params.opslimit, params.memlimit) != 0;
}

void random_bytes(std::span<unsigned char> out) {
  detail::ensure_sodium();
  randombytes_buf(out.data(), out.size());
}

std::string random_token_b64url(std::size_t bytes) {
  std::vector<unsigned char> buffer(bytes);
  random_bytes(buffer);
  return base64url_encode(buffer);
}

std::string random_hex(std::size_t bytes) {
  std::vector<unsigned char> buffer(bytes);
  random_bytes(buffer);
  std::string out(bytes * 2 + 1, '\0');
  sodium_bin2hex(out.data(), out.size(), buffer.data(), buffer.size());
  out.resize(bytes * 2);
  return out;
}

bool constant_time_equals(std::string_view a, std::string_view b) {
  detail::ensure_sodium();
  if (a.size() != b.size()) {
    return false;
  }
  return a.empty() || sodium_memcmp(a.data(), b.data(), a.size()) == 0;
}

std::string base64url_encode(std::span<const unsigned char> data) {
  return encode(data, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
}

std::string base64url_encode(std::string_view data) {
  return base64url_encode(
      std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(data.data()), data.size()));
}

std::vector<unsigned char> base64url_decode(std::string_view text) {
  std::vector<unsigned char> out;
  if (!try_decode(text, sodium_base64_VARIANT_URLSAFE_NO_PADDING, out)) {
    throw std::invalid_argument("invalid base64url");
  }
  return out;
}

std::string base64_encode(std::span<const unsigned char> data) { return encode(data, sodium_base64_VARIANT_ORIGINAL); }

std::vector<unsigned char> base64_decode(std::string_view text) {
  std::vector<unsigned char> out;
  if (try_decode(text, sodium_base64_VARIANT_ORIGINAL, out) ||
      try_decode(text, sodium_base64_VARIANT_ORIGINAL_NO_PADDING, out)) {
    return out;
  }
  throw std::invalid_argument("invalid base64");
}

std::vector<unsigned char> decode_key_b64(std::string_view text) {
  // Tolerate surrounding whitespace from copy-pasted keys or env files.
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    throw std::invalid_argument("empty key");
  }
  text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
  std::vector<unsigned char> out;
  for (int variant : {sodium_base64_VARIANT_ORIGINAL, sodium_base64_VARIANT_ORIGINAL_NO_PADDING,
                      sodium_base64_VARIANT_URLSAFE, sodium_base64_VARIANT_URLSAFE_NO_PADDING}) {
    if (try_decode(text, variant, out)) {
      return out;
    }
  }
  throw std::invalid_argument("key is not valid base64");
}

}  // namespace sg::common
