add_library(sg_analytics_lib STATIC
  src/read_model.cpp
  src/analytics_engine.cpp
  src/api.cpp
  src/event_consumer.cpp)
target_include_directories(sg_analytics_lib PUBLIC src)
target_link_libraries(sg_analytics_lib PUBLIC sg::common sg::graphcore)
sg_set_warnings(sg_analytics_lib)

add_executable(analytics-service src/main.cpp)
target_link_libraries(analytics-service PRIVATE sg_analytics_lib)
sg_set_warnings(analytics-service)
install(TARGETS analytics-service RUNTIME DESTINATION bin)

if(SG_BUILD_TESTS)
  add_executable(analytics_tests
    tests/read_model_test.cpp
    tests/analytics_engine_test.cpp
    tests/api_test.cpp
    tests/event_consumer_test.cpp)
  target_link_libraries(analytics_tests PRIVATE sg_analytics_lib GTest::gtest_main)
  sg_set_warnings(analytics_tests)
  gtest_discover_tests(analytics_tests DISCOVERY_TIMEOUT 30)
endif()
