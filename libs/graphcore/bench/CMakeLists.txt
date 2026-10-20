# graphcore_bench: Google Benchmark suite + legacy baseline (only built when benchmark::benchmark
# is available, see ../CMakeLists.txt).

add_executable(graphcore_bench
  graphcore_bench.cpp
  dataset.cpp
  legacy_baseline.cpp
)
target_link_libraries(graphcore_bench PRIVATE sg::graphcore benchmark::benchmark)
target_compile_definitions(graphcore_bench PRIVATE
  SG_DATASET_DIR="${PROJECT_SOURCE_DIR}/data/stackoverflow"
)

find_package(ZLIB QUIET)
if(ZLIB_FOUND)
  target_link_libraries(graphcore_bench PRIVATE ZLIB::ZLIB)
  target_compile_definitions(graphcore_bench PRIVATE SG_HAVE_ZLIB=1)
else()
  message(STATUS "graphcore: zlib not found, graphcore_bench reads only uncompressed .csv datasets")
  target_compile_definitions(graphcore_bench PRIVATE SG_HAVE_ZLIB=0)
endif()

sg_set_warnings(graphcore_bench)
sg_graphcore_fatal_ubsan(graphcore_bench)
