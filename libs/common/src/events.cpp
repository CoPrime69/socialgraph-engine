#include "sg/common/events.hpp"

#include <charconv>
#include <string_view>
#include <stdexcept>
#include <system_error>

namespace sg::common {
namespace {

/// Copy of a required, non-empty field (returned by value: entries are small).
std::string required_field(const StreamEntry& entry, std::string_view name) {
  const auto it = entry.fields.find(std::string(name));
  if (it == entry.fields.end() || it->second.empty()) {
    throw std::invalid_argument("stream entry " + entry.id + " has no '" + std::string(name) + "' field");
  }
  return it->second;
}

}  // namespace

std::vector<std::pair<std::string, std::string>> to_stream_fields(const Event& event) {
  return {
      {"event_id", event.event_id},
      {"type", event.type},
      {"schema", std::to_string(event.schema)},
      {"occurred_at", event.occurred_at},
      {"payload", event.payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)},
  };
}

Event from_stream_entry(const StreamEntry& entry) {
  Event event;
  event.event_id = required_field(entry, "event_id");
  event.type = required_field(entry, "type");
  event.occurred_at = required_field(entry, "occurred_at");

  const std::string schema = required_field(entry, "schema");
  int parsed = 0;
  const auto [ptr, ec] = std::from_chars(schema.data(), schema.data() + schema.size(), parsed);
  if (ec != std::errc() || ptr != schema.data() + schema.size() || parsed <= 0) {
    throw std::invalid_argument("stream entry " + entry.id + " has an invalid schema '" + schema + "'");
  }
  event.schema = parsed;

  const std::string payload = required_field(entry, "payload");
  event.payload = nlohmann::json::parse(payload, nullptr, /*allow_exceptions=*/false);
  if (!event.payload.is_object()) {
    throw std::invalid_argument("stream entry " + entry.id + " has a payload that is not a JSON object");
  }
  return event;
}

}  // namespace sg::common
