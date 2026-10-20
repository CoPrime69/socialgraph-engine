#pragma once

/// @file pqxx.hpp
/// The one way to include libpqxx in this code base: `#include "sg/common/pqxx.hpp"`, never
/// `<pqxx/...>` directly.
///
/// Ubuntu 24.04 ships libpqxx 7.8 compiled as C++17. Its headers add `std::source_location`
/// parameters (and a `location` member) to the pqxx exception classes whenever the *including*
/// code is C++20, which changes their layout and references constructors the shared library does
/// not export (undefined references to e.g. `pqxx::usage_error::usage_error(..., source_location)`).
/// Hiding the feature-test macro while the pqxx headers are parsed makes them match the library.
///
/// GCC 13 at -O2/-O3 also reports a false -Wmaybe-uninitialized inside `pqxx::params::append<T>()`
/// (the move of its std::variant entry) wherever `exec_params()` gets a non-string argument. GCC
/// matches diagnostic pragmas against every location of an inlined call chain, so ignoring that
/// warning for the lines of the pqxx headers silences these reports without disabling it for the
/// including code.

#include <version>

#if defined(__cpp_lib_source_location)
#include <source_location>
#pragma push_macro("__cpp_lib_source_location")
#undef __cpp_lib_source_location
#define SG_COMMON_PQXX_RESTORE_SOURCE_LOCATION
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <pqxx/pqxx>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#if defined(SG_COMMON_PQXX_RESTORE_SOURCE_LOCATION)
#pragma pop_macro("__cpp_lib_source_location")
#undef SG_COMMON_PQXX_RESTORE_SOURCE_LOCATION
#endif
