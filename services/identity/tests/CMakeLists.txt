# Unit tests: domain rules, application layer with in-memory fakes, HTTP API on an ephemeral port
# and the taxonomy client against a fake graph-service. No external services needed.
add_executable(identity_unit_tests
  app_test.cpp
  domain_test.cpp
  http_api_test.cpp
  taxonomy_client_test.cpp)
target_link_libraries(identity_unit_tests PRIVATE sg_identity_core sg::common_testing GTest::gtest_main)
sg_set_warnings(identity_unit_tests)
gtest_discover_tests(identity_unit_tests DISCOVERY_TIMEOUT 60 PROPERTIES LABELS unit)

# PostgreSQL (SG_TEST_DATABASE_URL) and Redis (SG_TEST_REDIS_HOST/PORT) integration tests; each
# test skips itself when its dependency is not configured.
add_executable(identity_integration_tests postgres_test.cpp)
target_link_libraries(identity_integration_tests PRIVATE sg_identity_core sg::common_testing GTest::gtest_main)
sg_set_warnings(identity_integration_tests)
gtest_discover_tests(identity_integration_tests DISCOVERY_TIMEOUT 60 PROPERTIES LABELS integration)
