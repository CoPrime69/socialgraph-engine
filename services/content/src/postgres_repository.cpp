#include "postgres_repository.hpp"

#include <sstream>

namespace sg::content {

namespace {

// Formats a timestamptz column as RFC 3339 UTC in SQL so C++ never parses dates.
#define SG_RFC3339(col) "to_char(" col " AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')"

constexpr const char* kQuestionTags =
    "(SELECT coalesce(string_agg(t.name, ' ' ORDER BY qt.position), '') FROM question_tags qt "
    "JOIN tags t ON t.id = qt.tag_id WHERE qt.question_id = q.id)";

std::optional<AuthorRef> author(const pqxx::row& row, int id_col, int name_col) {
  if (row[id_col].is_null()) return std::nullopt;
  return AuthorRef{row[id_col].as<std::int64_t>(),
                   row[name_col].is_null() ? std::string{} : row[name_col].as<std::string>()};
}

QuestionSummary question_summary(const pqxx::row& r) {
  // columns: id, title, score, view_count, answer_count, created_at, owner_id, display_name, tags
  QuestionSummary q;
  q.id = r[0].as<std::int64_t>();
  q.title = r[1].as<std::string>();
  q.score = r[2].as<std::int64_t>();
  q.view_count = r[3].as<std::int64_t>();
  q.answer_count = r[4].as<std::int64_t>();
  q.created_at = r[5].as<std::string>();
  q.owner = author(r, 6, 7);
  q.tags = split_tags(r[8].as<std::string>());
  return q;
}

constexpr const char* kQuestionColumns =
    "q.id, q.title, q.score, q.view_count, q.answer_count, " SG_RFC3339("q.created_at")
    ", q.owner_id, au.display_name, ";

}  // namespace

std::string escape_like(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    if (c == '%' || c == '_' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

std::vector<std::string> split_tags(const std::string& joined) {
  std::vector<std::string> tags;
  std::istringstream in(joined);
  for (std::string tag; in >> tag;) tags.push_back(tag);
  return tags;
}

Activity PostgresContentRepository::activity(std::int64_t user_id) {
  return pool_.with_read([&](pqxx::nontransaction& tx) {
    Activity a;
    a.user_id = user_id;
    // Precomputed at import (migration 0002); members without any content have no row.
    const auto rows = tx.exec_params(
        "SELECT questions, question_score, answers, accepted_answers, answer_score, comments, gold, silver, bronze "
        "FROM user_activity WHERE user_id = $1",
        user_id);
    if (rows.empty()) {
      return a;
    }
    const auto& totals = rows[0];
    a.totals.questions = totals[0].as<std::int64_t>();
    a.totals.question_score = totals[1].as<std::int64_t>();
    a.totals.answers = totals[2].as<std::int64_t>();
    a.totals.accepted_answers = totals[3].as<std::int64_t>();
    a.totals.answer_score = totals[4].as<std::int64_t>();
    a.totals.comments = totals[5].as<std::int64_t>();
    a.gold = totals[6].as<std::int64_t>();
    a.silver = totals[7].as<std::int64_t>();
    a.bronze = totals[8].as<std::int64_t>();

    for (const auto& r : tx.exec_params(
             "SELECT t.name, ut.posts, ut.score FROM user_tags ut JOIN tags t ON t.id = ut.tag_id "
             "WHERE ut.user_id = $1 ORDER BY ut.posts DESC, ut.score DESC, t.name LIMIT 8",
             user_id)) {
      a.top_tags.push_back({r[0].as<std::string>(), r[1].as<std::int64_t>(), r[2].as<std::int64_t>()});
    }
    for (const auto& r : tx.exec_params("SELECT name, class, " SG_RFC3339("awarded_at")
                                        " FROM badges WHERE user_id = $1 ORDER BY awarded_at DESC, id DESC LIMIT 8",
                                        user_id)) {
      a.recent_badges.push_back({r[0].as<std::string>(), r[1].as<int>(), r[2].as<std::string>()});
    }
    const std::string top_posts =
        std::string("SELECT * FROM ("
                    " (SELECT true, q.id, q.id, q.title, q.score, q.view_count, " SG_RFC3339("q.created_at") ", ") +
        kQuestionTags +
        " FROM questions q WHERE q.owner_id = $1 ORDER BY q.score DESC, q.id LIMIT 5)"
        " UNION ALL"
        " (SELECT false, a.id, q.id, q.title, a.score, NULL::integer, " SG_RFC3339("a.created_at") ", " +
        kQuestionTags +
        " FROM answers a JOIN questions q ON q.id = a.question_id WHERE a.owner_id = $1"
        "  ORDER BY a.score DESC, a.id LIMIT 5)"
        ") posts ORDER BY 5 DESC, 2 LIMIT 5";
    for (const auto& r : tx.exec_params(top_posts, user_id)) {
      PostSummary p;
      p.is_question = r[0].as<bool>();
      p.id = r[1].as<std::int64_t>();
      p.question_id = r[2].as<std::int64_t>();
      p.title = r[3].as<std::string>();
      p.score = r[4].as<std::int64_t>();
      if (!r[5].is_null()) p.view_count = r[5].as<std::int64_t>();
      p.created_at = r[6].as<std::string>();
      p.tags = split_tags(r[7].as<std::string>());
      a.top_posts.push_back(std::move(p));
    }
    return a;
  });
}

Page<TagInfo> PostgresContentRepository::tags(const std::optional<std::string>& prefix, std::int64_t limit,
                                               std::int64_t offset) {
  return pool_.with_read([&](pqxx::nontransaction& tx) {
    std::optional<std::string> pattern;
    if (prefix) pattern = escape_like(*prefix) + "%";
    Page<TagInfo> page{{}, 0, limit, offset};
    page.total = tx.exec_params1("SELECT count(*) FROM tags WHERE $1::text IS NULL OR name LIKE $1 ESCAPE '\\'",
                                 pattern)[0]
                     .as<std::int64_t>();
    for (const auto& r : tx.exec_params(
             "SELECT t.name, t.question_count, c.name FROM tags t LEFT JOIN categories c ON c.slug = t.category_slug "
             "WHERE $1::text IS NULL OR t.name LIKE $1 ESCAPE '\\' "
             "ORDER BY t.question_count DESC, t.name LIMIT $2 OFFSET $3",
             pattern, limit, offset)) {
      TagInfo tag{r[0].as<std::string>(), r[1].as<std::int64_t>(), std::nullopt};
      if (!r[2].is_null()) tag.category = r[2].as<std::string>();
      page.items.push_back(std::move(tag));
    }
    return page;
  });
}

Page<QuestionSummary> PostgresContentRepository::questions(const QuestionQuery& query) {
  return pool_.with_read([&](pqxx::nontransaction& tx) {
    Page<QuestionSummary> page{{}, 0, query.limit, query.offset};
    std::optional<std::int64_t> tag_id;
    if (query.tag) {
      auto rows = tx.exec_params("SELECT id FROM tags WHERE name = $1", *query.tag);
      if (rows.empty()) return page;  // unknown tag: nothing matches
      tag_id = rows[0][0].as<std::int64_t>();
    }
    const std::string where =
        " WHERE ($1::integer IS NULL OR q.id IN (SELECT question_id FROM question_tags WHERE tag_id = $1))"
        " AND ($2::text IS NULL OR q.search @@ websearch_to_tsquery('english', $2))";
    const char* order =
        query.sort == QuestionSort::recent ? " ORDER BY q.created_at DESC, q.id DESC" : " ORDER BY q.score DESC, q.id";
    page.total = tx.exec_params1("SELECT count(*) FROM questions q" + where, tag_id, query.text)[0].as<std::int64_t>();
    const std::string sql = std::string("SELECT ") + kQuestionColumns + kQuestionTags +
                            " FROM questions q LEFT JOIN authors au ON au.user_id = q.owner_id" + where + order +
                            " LIMIT $3 OFFSET $4";
    for (const auto& r : tx.exec_params(sql, tag_id, query.text, query.limit, query.offset)) {
      page.items.push_back(question_summary(r));
    }
    return page;
  });
}

std::optional<QuestionDetail> PostgresContentRepository::question(std::int64_t id) {
  return pool_.with_read([&](pqxx::nontransaction& tx) -> std::optional<QuestionDetail> {
    const std::string sql = std::string("SELECT ") + kQuestionColumns + kQuestionTags +
                            ", q.accepted_answer_id, " SG_RFC3339("q.closed_at") ","
                            " (SELECT count(*) FROM comments c WHERE c.question_id = q.id)"
                            " FROM questions q LEFT JOIN authors au ON au.user_id = q.owner_id WHERE q.id = $1";
    auto rows = tx.exec_params(sql, id);
    if (rows.empty()) return std::nullopt;
    QuestionDetail detail;
    detail.question = question_summary(rows[0]);
    if (!rows[0][9].is_null()) detail.accepted_answer_id = rows[0][9].as<std::int64_t>();
    if (!rows[0][10].is_null()) detail.closed_at = rows[0][10].as<std::string>();
    detail.comment_count = rows[0][11].as<std::int64_t>();
    for (const auto& r : tx.exec_params("SELECT a.id, a.owner_id, au.display_name, a.score, " SG_RFC3339("a.created_at")
                                        ", a.is_accepted FROM answers a LEFT JOIN authors au ON au.user_id = a.owner_id"
                                        " WHERE a.question_id = $1 ORDER BY a.is_accepted DESC, a.score DESC, a.id",
                                        id)) {
      detail.answers.push_back({r[0].as<std::int64_t>(), author(r, 1, 2), r[3].as<std::int64_t>(),
                                r[4].as<std::string>(), r[5].as<bool>()});
    }
    return detail;
  });
}

}  // namespace sg::content
