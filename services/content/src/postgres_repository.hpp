#pragma once

#include "model.hpp"

#include <sg/common/pg.hpp>

namespace sg::content {

// Read-only queries over the content schema (migrations/0001). Every value is passed as a bound
// parameter; only fixed ORDER BY fragments chosen from an enum are spliced into SQL text.
class PostgresContentRepository final : public ContentRepository {
 public:
  explicit PostgresContentRepository(sg::common::PgPool& pool) : pool_(pool) {}

  Activity activity(std::int64_t user_id) override;
  Page<TagInfo> tags(const std::optional<std::string>& prefix, std::int64_t limit, std::int64_t offset) override;
  Page<QuestionSummary> questions(const QuestionQuery& query) override;
  std::optional<QuestionDetail> question(std::int64_t id) override;
  bool ping() override { return pool_.ping(); }

 private:
  sg::common::PgPool& pool_;
};

// Escapes LIKE wildcards so user input matches literally (used with ESCAPE '\').
std::string escape_like(const std::string& value);
std::vector<std::string> split_tags(const std::string& joined);

}  // namespace sg::content
