# sg::common: shared runtime of the C++ services (HTTP server, JSON logging, config, errors,
# Ed25519 tokens, password hashing, PostgreSQL pool + migrations, Redis Streams, transactional outbox).

# The HTTP server runs on Drogon (libdrogon-dev). Only src/http.cpp includes Drogon headers, so it is
# a private dependency. The distribution's DrogonConfig.cmake insists on the development packages of
# every optional backend Drogon was built with (MySQL, SQLite, yaml-cpp, ...), none of which are
# used here, so the library and headers are located directly; libdrogon.so pulls in its own runtime
# dependencies. Drogon's headers include <json/json.h> from jsoncpp (libjsoncpp-dev).
find_package(Trantor CONFIG REQUIRED)
find_path(DROGON_INCLUDE_DIR drogon/drogon.h REQUIRED)
find_library(DROGON_LIBRARY drogon REQUIRED)
find_path(JSONCPP_INCLUDE_DIR json/json.h PATH_SUFFIXES jsoncpp REQUIRED)
find_library(JSONCPP_LIBRARY jsoncpp REQUIRED)
add_library(sg_drogon INTERFACE IMPORTED)
set_target_properties(sg_drogon PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${DROGON_INCLUDE_DIR};${JSONCPP_INCLUDE_DIR}"
  INTERFACE_LINK_LIBRARIES "${DROGON_LIBRARY};Trantor::Trantor;${JSONCPP_LIBRARY}")

# Service-to-service and healthcheck clients use cpp-httplib's Client.
# The Debian/Ubuntu libcpp-httplib-dev package ships a *split* httplib.h (declarations only) plus
# libcpp-httplib.so, compiled with OpenSSL/zlib/Brotli support. The same CPPHTTPLIB_* definitions
# must be visible to every translation unit that includes httplib.h, otherwise class layouts differ
# from the compiled library (ODR violation, memory corruption). pkg-config provides exactly those
# flags and the library, so sg_common exports them PUBLIC to all consumers.
pkg_check_modules(CPPHTTPLIB REQUIRED IMPORTED_TARGET cpp-httplib)
# gzip-compressed dataset files (CsvReader). zlib1g-dev is a dependency of libcpp-httplib-dev.
find_package(ZLIB REQUIRED)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedMigrations.cmake)

add_library(sg_common STATIC
  src/auth.cpp
  src/config.cpp
  src/crypto.cpp
  src/csv.cpp
  src/errors.cpp
  src/events.cpp
  src/http.cpp
  src/lifecycle.cpp
  src/log.cpp
  src/outbox.cpp
  src/pg.cpp
  src/rate_limit.cpp
  src/redis.cpp
  src/time.cpp
  src/validation.cpp)
add_library(sg::common ALIAS sg_common)
target_include_directories(sg_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(sg_common PUBLIC cxx_std_20)
target_link_libraries(sg_common
  PUBLIC nlohmann_json::nlohmann_json PkgConfig::PQXX PkgConfig::CPPHTTPLIB Threads::Threads
  PRIVATE sg_drogon PkgConfig::SODIUM PkgConfig::HIREDIS ZLIB::ZLIB)
sg_set_warnings(sg_common)

# Test-only helpers shared by the service test suites (temporary databases, env guards).
add_library(sg_common_testing INTERFACE)
add_library(sg::common_testing ALIAS sg_common_testing)
target_include_directories(sg_common_testing INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/testing/include)
target_link_libraries(sg_common_testing INTERFACE sg::common)

if(SG_BUILD_TESTS)
  add_subdirectory(tests)
endif()
