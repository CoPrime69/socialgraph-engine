# Unit tests: domain rules, application layer with an in-memory repository and the HTTP API on an
# ephemeral port. No external services needed.
add_executable(graph_unit_tests
  app_test.cpp
  domain_test.cpp
  http_api_test.cpp
  read_cache_test.cpp)
target_link_libraries(graph_unit_tests PRIVATE sg_graph_service_core sg::common_testing GTest::gtest_main)
sg_set_warnings(graph_unit_tests)
gtest_discover_tests(graph_unit_tests DISCOVERY_TIMEOUT 60 PROPERTIES LABELS unit)

# PostgreSQL (SG_TEST_DATABASE_URL) and Redis (SG_TEST_REDIS_HOST/PORT) integration tests, including the
# cross-service event flow identity -> graph; each test skips itself when a dependency is missing.
set(graph_integration_sources postgres_test.cpp seed_test.cpp consumer_test.cpp)
if(TARGET sg_identity_core)
  list(APPEND graph_integration_sources event_flow_test.cpp)
endif()
add_executable(graph_integration_tests ${graph_integration_sources})
target_link_libraries(graph_integration_tests PRIVATE sg_graph_service_core sg::common_testing GTest::gtest_main)
if(TARGET sg_identity_core)
  target_link_libraries(graph_integration_tests PRIVATE sg_identity_core)
endif()
target_compile_definitions(graph_integration_tests PRIVATE
  SG_GRAPH_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  SG_DATASET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../data/stackoverflow")
sg_set_warnings(graph_integration_tests)
gtest_discover_tests(graph_integration_tests DISCOVERY_TIMEOUT 60 PROPERTIES LABELS integration)
