#pragma once

/// @file migration.hpp
/// A versioned SQL schema migration (see run_migrations() in pg.hpp and sg_embed_migrations()).

#include <string>

namespace sg::common {

/// One schema migration. `version` is strictly increasing within a service; `name` must never
/// change once the migration has been applied anywhere (run_migrations() verifies it).
struct Migration {
  int version;
  std::string name;
  std::string sql;
};

}  // namespace sg::common
