add_library(sg_content_lib STATIC
  src/api.cpp
  src/postgres_repository.cpp
  src/seed_loader.cpp)
target_include_directories(sg_content_lib PUBLIC src)
target_link_libraries(sg_content_lib PUBLIC sg::common)
sg_embed_migrations(sg_content_lib migrations sg::content)
sg_set_warnings(sg_content_lib)

add_executable(content-service src/main.cpp)
target_link_libraries(content-service PRIVATE sg_content_lib)
sg_set_warnings(content-service)
install(TARGETS content-service RUNTIME DESTINATION bin)

if(SG_BUILD_TESTS)
  add_executable(content_tests tests/api_test.cpp tests/postgres_test.cpp)
  target_link_libraries(content_tests PRIVATE sg_content_lib GTest::gtest_main)
  target_compile_definitions(content_tests PRIVATE
    SG_CONTENT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  sg_set_warnings(content_tests)
  gtest_discover_tests(content_tests DISCOVERY_TIMEOUT 30)
endif()
