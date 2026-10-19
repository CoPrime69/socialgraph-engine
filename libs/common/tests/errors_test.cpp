#include <gtest/gtest.h>

#include "sg/common/errors.hpp"

namespace {

using namespace sg::common;

TEST(ApiError, JsonEnvelopeWithoutDetails) {
  const ApiError e = not_found("User not found");
  EXPECT_EQ(e.status(), 404);
  EXPECT_EQ(e.code(), "not_found");
  EXPECT_EQ(e.to_json(), nlohmann::json::parse(R"({"error":{"code":"not_found","message":"User not found"}})"));
}

TEST(ApiError, JsonEnvelopeWithDetails) {
  const ApiError e = validation_failed({{"handle", "must be 3-32 characters of a-z, 0-9 or _"}, {"name", "required"}});
  EXPECT_EQ(e.status(), 422);
  EXPECT_EQ(e.to_json(), nlohmann::json::parse(R"({"error":{"code":"validation_failed","message":"Request validation failed",
      "details":[{"field":"handle","message":"must be 3-32 characters of a-z, 0-9 or _"},{"field":"name","message":"required"}]}})"));
}

TEST(ApiError, FactoriesUseContractCodes) {
  EXPECT_EQ(bad_request("x").status(), 400);
  EXPECT_EQ(bad_request("x").code(), "bad_request");
  EXPECT_EQ(unauthenticated().status(), 401);
  EXPECT_EQ(unauthenticated().code(), "unauthenticated");
  EXPECT_EQ(unauthenticated().message(), "Authentication required");
  EXPECT_EQ(invalid_credentials().status(), 401);
  EXPECT_EQ(invalid_credentials().code(), "invalid_credentials");
  EXPECT_EQ(conflict("x").status(), 409);
  EXPECT_EQ(conflict("x").code(), "conflict");
  EXPECT_EQ(payload_too_large(65536).status(), 413);
  EXPECT_EQ(payload_too_large(65536).code(), "payload_too_large");
  EXPECT_EQ(unavailable("x").status(), 503);
  EXPECT_EQ(unavailable("x").code(), "unavailable");
  EXPECT_EQ(internal_error().status(), 500);
  EXPECT_EQ(internal_error().code(), "internal");
  EXPECT_FALSE(bad_request("x").retry_after().has_value());
}

TEST(ApiError, RateLimitedCarriesRetryAfter) {
  const ApiError e = rate_limited(17);
  EXPECT_EQ(e.status(), 429);
  EXPECT_EQ(e.code(), "rate_limited");
  ASSERT_TRUE(e.retry_after().has_value());
  EXPECT_EQ(*e.retry_after(), 17);
  EXPECT_EQ(*rate_limited(0).retry_after(), 1);  // never "retry after 0 s"
}

TEST(Validator, CollectsAllFailures) {
  Validator v;
  v.require(true, "a", "ok");
  EXPECT_TRUE(v.ok());
  EXPECT_NO_THROW(v.throw_if_failed());
  v.require(false, "b", "bad b");
  v.add("c", "bad c");
  EXPECT_FALSE(v.ok());
  ASSERT_EQ(v.errors().size(), 2U);
  try {
    v.throw_if_failed();
    FAIL() << "expected validation_failed";
  } catch (const ApiError& e) {
    EXPECT_EQ(e.status(), 422);
    ASSERT_EQ(e.details().size(), 2U);
    EXPECT_EQ(e.details()[0], (FieldError{"b", "bad b"}));
    EXPECT_EQ(e.details()[1], (FieldError{"c", "bad c"}));
  }
}

}  // namespace
