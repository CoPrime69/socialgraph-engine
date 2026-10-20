#pragma once

/// @file redis.hpp
/// Minimal synchronous Redis client for Streams (hiredis). Not thread-safe: use one client per
/// thread. Commands are sent as argument vectors (binary safe, no injection). The connection is
/// opened lazily; after an I/O error it is dropped and re-opened by the next command.

#include <chrono>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sg::common {

struct RedisOptions {
  std::string host = "127.0.0.1";
  int port = 6379;
  std::chrono::milliseconds timeout{2000};  ///< connect and per-command timeout (plus BLOCK time)
};

/// Connection failures and error replies.
class RedisError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct StreamEntry {
  std::string id;
  std::unordered_map<std::string, std::string> fields;
};

class RedisClient {
 public:
  explicit RedisClient(RedisOptions options);
  ~RedisClient();
  RedisClient(const RedisClient&) = delete;
  RedisClient& operator=(const RedisClient&) = delete;

  /// XADD stream * field value ... ; returns the entry id.
  std::string xadd(const std::string& stream, const std::vector<std::pair<std::string, std::string>>& fields);
  /// XADD stream MAXLEN ~ approx_max_len * ... (approx_max_len == 0: no trimming).
  std::string xadd(const std::string& stream, const std::vector<std::pair<std::string, std::string>>& fields,
                   std::size_t approx_max_len);

  /// XREAD COUNT count [BLOCK ms] STREAMS stream after_id. `block` == 0 does not block.
  /// Returns the entries after `after_id` (empty on timeout).
  std::vector<StreamEntry> xread(const std::string& stream, const std::string& after_id,
                                 std::chrono::milliseconds block, std::size_t count);

  /// XGROUP CREATE stream group start_id MKSTREAM; an existing group (BUSYGROUP) is not an error.
  void xgroup_create(const std::string& stream, const std::string& group, const std::string& start_id = "0");

  /// XREADGROUP GROUP group consumer COUNT count [BLOCK ms] STREAMS stream id.
  /// id ">" = new entries; "0" = this consumer's pending (delivered, unacknowledged) entries.
  std::vector<StreamEntry> xreadgroup(const std::string& stream, const std::string& group,
                                      const std::string& consumer, std::chrono::milliseconds block,
                                      std::size_t count, const std::string& id = ">");

  /// XACK stream group id.
  void xack(const std::string& stream, const std::string& group, const std::string& id);

  struct AutoClaimed {
    std::string next_start_id;  ///< "0-0" when the scan is complete
    std::vector<StreamEntry> entries;
  };
  /// XAUTOCLAIM stream group consumer min_idle start_id COUNT count: takes over entries other
  /// (crashed) consumers left pending for longer than `min_idle`.
  AutoClaimed xautoclaim(const std::string& stream, const std::string& group, const std::string& consumer,
                         std::chrono::milliseconds min_idle, const std::string& start_id, std::size_t count);

  /// XRANGE stream start end [COUNT count] (count == 0: all).
  std::vector<StreamEntry> xrange(const std::string& stream, const std::string& start = "-",
                                  const std::string& end = "+", std::size_t count = 0);

  /// XLEN stream.
  long long xlen(const std::string& stream);

  /// DEL key; returns the number of keys removed.
  long long del(const std::string& key);

  /// PING; false instead of throwing when Redis is unreachable.
  bool ping();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace sg::common
