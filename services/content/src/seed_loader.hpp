#pragma once

#include <sg/common/pg.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace sg::content {

struct SeedReport {
  bool loaded = false;  // false when the database already had content
  std::int64_t authors = 0, tags = 0, questions = 0, question_tags = 0, answers = 0, comments = 0, badges = 0,
               user_tags = 0;
  double seconds = 0.0;
};

// Bulk-loads data/stackoverflow/*.csv[.gz] (contracts §10.1) with COPY inside one transaction, so a
// failed import leaves the database empty. Guarded by an advisory lock and an emptiness check, so
// concurrent replicas import exactly once.
SeedReport load_seed_if_empty(sg::common::PgPool& pool, const std::filesystem::path& directory);

}  // namespace sg::content
