#include <gtest/gtest.h>

#include "sg/common/events.hpp"

namespace {

using namespace sg::common;

StreamEntry entry_from(const std::vector<std::pair<std::string, std::string>>& fields) {
  StreamEntry entry;
  entry.id = "1-0";
  for (const auto& [k, v] : fields) {
    entry.fields[k] = v;
  }
  return entry;
}

TEST(Events, RoundTripThroughStreamFields) {
  Event event;
  event.event_id = "42";
  event.type = "connection.added";
  event.occurred_at = "2026-10-19T20:00:00.000Z";
  event.payload = {{"graph_version", 7}, {"user_a", 1}, {"user_b", 2}, {"strength", 0.912}};
  const auto fields = to_stream_fields(event);
  ASSERT_EQ(fields.size(), 5U);
  EXPECT_EQ(fields[0], (std::pair<std::string, std::string>{"event_id", "42"}));
  EXPECT_EQ(fields[2], (std::pair<std::string, std::string>{"schema", "1"}));

  const Event parsed = from_stream_entry(entry_from(fields));
  EXPECT_EQ(parsed.event_id, "42");
  EXPECT_EQ(parsed.type, "connection.added");
  EXPECT_EQ(parsed.schema, 1);
  EXPECT_EQ(parsed.occurred_at, event.occurred_at);
  EXPECT_EQ(parsed.payload, event.payload);
}

TEST(Events, MalformedEntriesThrowInvalidArgument) {
  const std::vector<std::pair<std::string, std::string>> good{
      {"event_id", "1"}, {"type", "user.registered"}, {"schema", "1"}, {"occurred_at", "2026-01-01T00:00:00Z"},
      {"payload", "{}"}};
  EXPECT_NO_THROW((void)from_stream_entry(entry_from(good)));
  for (const std::string missing : {"event_id", "type", "schema", "occurred_at", "payload"}) {
    auto fields = good;
    std::erase_if(fields, [&](const auto& f) { return f.first == missing; });
    EXPECT_THROW((void)from_stream_entry(entry_from(fields)), std::invalid_argument) << missing;
  }
  for (const std::string payload : {"not json", "[1,2]", "\"text\"", "{"}) {
    auto fields = good;
    fields.back().second = payload;
    EXPECT_THROW((void)from_stream_entry(entry_from(fields)), std::invalid_argument) << payload;
  }
  for (const std::string schema : {"0", "-1", "one", "1x"}) {
    auto fields = good;
    fields[2].second = schema;
    EXPECT_THROW((void)from_stream_entry(entry_from(fields)), std::invalid_argument) << schema;
  }
}

}  // namespace
