#pragma once

/// @file events.hpp
/// Event envelope on Redis Streams (contracts.md §7): fields event_id, type, schema, occurred_at
/// and payload (a JSON object serialised as a string).

#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "sg/common/redis.hpp"

namespace sg::common {

struct Event {
  std::string event_id;     ///< producer outbox id
  std::string type;         ///< e.g. "user.registered"
  int schema = 1;
  std::string occurred_at;  ///< RFC 3339
  nlohmann::json payload;   ///< JSON object
};

/// Stream fields of `event` in contract order.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> to_stream_fields(const Event& event);

/// Parses and validates a stream entry.
/// @throws std::invalid_argument when a field is missing or empty, schema is not a positive
///         integer, or payload is not a JSON object.
[[nodiscard]] Event from_stream_entry(const StreamEntry& entry);

}  // namespace sg::common
