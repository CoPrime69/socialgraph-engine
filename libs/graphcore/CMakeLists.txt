# sg::graphcore — pure C++20 graph algorithms (no I/O). See README.md.

add_library(sg_graphcore STATIC
  src/affinity.cpp
  src/centrality.cpp
  src/community.cpp
  src/components.cpp
  src/csr_graph.cpp
  src/generator.cpp
  src/paths.cpp
  src/recommend.cpp
)
add_library(sg::graphcore ALIAS sg_graphcore)

target_include_directories(sg_graphcore PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(sg_graphcore PUBLIC cxx_std_20)
target_link_libraries(sg_graphcore PUBLIC Threads::Threads)
set_target_properties(sg_graphcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
sg_set_warnings(sg_graphcore)

# In UBSan builds (SG_SANITIZE contains "undefined") undefined behaviour in graphcore's own code
# aborts instead of printing a diagnostic and continuing, so ctest fails on it.
function(sg_graphcore_fatal_ubsan target)
  if(SG_SANITIZE MATCHES "undefined")
    target_compile_options(${target} PRIVATE -fno-sanitize-recover=undefined)
  endif()
endfunction()
sg_graphcore_fatal_ubsan(sg_graphcore)

if(SG_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(SG_BUILD_BENCHMARKS)
  find_package(benchmark QUIET)
  if(benchmark_FOUND)
    add_subdirectory(bench)
  else()
    message(STATUS "graphcore: Google Benchmark not found, graphcore_bench is not built")
  endif()
endif()
