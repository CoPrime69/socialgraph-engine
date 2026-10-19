add_executable(graphcore_tests
  test_support.cpp
  affinity_test.cpp
  betweenness_test.cpp
  closeness_test.cpp
  community_test.cpp
  csr_graph_test.cpp
  generator_test.cpp
  legacy_regression_test.cpp
  pagerank_test.cpp
  parallel_test.cpp
  paths_test.cpp
  recommend_test.cpp
)
target_link_libraries(graphcore_tests PRIVATE sg::graphcore GTest::gtest_main)
# Only used to cross-check the embedded seed fixture; the tests skip that check if it is absent.
target_compile_definitions(graphcore_tests PRIVATE
  SG_LEGACY_USERS_CSV="${PROJECT_SOURCE_DIR}/legacy/user_data.csv"
)
sg_set_warnings(graphcore_tests)
sg_graphcore_fatal_ubsan(graphcore_tests)

gtest_discover_tests(graphcore_tests
  DISCOVERY_TIMEOUT 120
  PROPERTIES LABELS graphcore
)
