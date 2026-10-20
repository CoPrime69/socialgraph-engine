#include <gtest/gtest.h>

#include "sg/common/time.hpp"

namespace {

using namespace sg::common;
using namespace std::chrono;

TEST(Time, FormatsRfc3339Utc) {
  const system_clock::time_point tp{seconds(1'792'440'000)};
  EXPECT_EQ(to_rfc3339(tp), "2026-10-19T20:00:00Z");
  EXPECT_EQ(to_rfc3339(tp + milliseconds(999)), "2026-10-19T20:00:00Z");
  EXPECT_EQ(to_rfc3339_millis(tp + milliseconds(7)), "2026-10-19T20:00:00.007Z");
  EXPECT_EQ(unix_to_rfc3339(0), "1970-01-01T00:00:00Z");
  EXPECT_EQ(unix_to_rfc3339(1'220'000'000), "2008-08-29T08:53:20Z");
}

TEST(Time, UnixNowIsCurrent) {
  const auto now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
  EXPECT_NEAR(static_cast<double>(unix_now()), static_cast<double>(now), 1.0);
}

}  // namespace
