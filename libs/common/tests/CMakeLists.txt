# Unit tests: no external services.
add_executable(sg_common_tests
  auth_test.cpp
  config_test.cpp
  crypto_test.cpp
  csv_test.cpp
  errors_test.cpp
  events_test.cpp
  http_test.cpp
  log_test.cpp
  lru_cache_test.cpp
  rate_limit_test.cpp
  time_test.cpp
  validation_test.cpp)
target_link_libraries(sg_common_tests PRIVATE sg::common sg::common_testing GTest::gtest_main ZLIB::ZLIB PkgConfig::SODIUM)
sg_set_warnings(sg_common_tests)
gtest_discover_tests(sg_common_tests DISCOVERY_TIMEOUT 60 PROPERTIES LABELS unit)

# Integration tests: PostgreSQL (SG_TEST_DATABASE_URL) and Redis (SG_TEST_REDIS_HOST/PORT);
# every test skips itself when its dependency is not configured.
add_executable(sg_common_integration_tests
  pg_test.cpp
  redis_test.cpp
  outbox_test.cpp)
target_link_libraries(sg_common_integration_tests PRIVATE sg::common sg::common_testing GTest::gtest_main)
sg_set_warnings(sg_common_integration_tests)
gtest_discover_tests(sg_common_integration_tests DISCOVERY_TIMEOUT 60 PROPERTIES LABELS integration)
