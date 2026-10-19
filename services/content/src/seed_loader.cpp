#include "seed_loader.hpp"

#include <sg/common/csv.hpp>
#include <sg/common/log.hpp>

#include <array>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sg::content {

namespace {

using Row = std::vector<std::optional<std::string>>;
constexpr std::int64_t kSeedLockKey = 0x5347'434f'4e54;  // "SGCONT"

std::filesystem::path resolve(const std::filesystem::path& dir, const std::string& name) {
  for (const auto& candidate : {dir / (name + ".csv.gz"), dir / (name + ".csv")}) {
    if (std::filesystem::exists(candidate)) return candidate;
  }
  throw std::runtime_error("seed file " + name + ".csv[.gz] not found in " + dir.string());
}

std::optional<std::string> nullable(std::string value) {
  if (value.empty()) return std::nullopt;
  return value;
}

// Streams one CSV file, checking its header, and hands each record to `fn`.
template <class F>
void each(const std::filesystem::path& dir, const std::string& name, const std::vector<std::string_view>& header,
          F&& fn) {
  sg::common::CsvReader reader(resolve(dir, name));
  reader.expect_header(header);
  while (auto record = reader.next()) {
    if (record->fields.size() != header.size()) {
      throw sg::common::CsvError(record->line, "expected " + std::to_string(header.size()) + " fields",
                                 reader.path().string());
    }
    fn(record->fields);
  }
}

}  // namespace

SeedReport load_seed_if_empty(sg::common::PgPool& pool, const std::filesystem::path& directory) {
  const auto started = std::chrono::steady_clock::now();
  SeedReport report;
  pool.with_transaction([&](pqxx::work& tx) {
    tx.exec("SET LOCAL statement_timeout = 0");
    tx.exec_params("SELECT pg_advisory_xact_lock($1)", kSeedLockKey);
    if (tx.exec1("SELECT EXISTS (SELECT 1 FROM authors)")[0].as<bool>()) return;
    sg::common::log_info("importing content seed", {{"directory", directory.string()}});

    {
      auto s = pqxx::stream_to::raw_table(tx, "categories", "slug, name, position");
      int position = 0;
      each(directory, "categories", {"slug", "name", "description"}, [&](std::vector<std::string>& f) {
        s.write_values(f[0], f[1], position++);
      });
      s.complete();
    }

    std::unordered_set<std::int64_t> authors;
    {
      auto s = pqxx::stream_to::raw_table(tx, "authors", "user_id, display_name, reputation");
      each(directory, "users",
           {"id", "handle", "display_name", "reputation", "influence", "created_at", "last_access_at", "location",
            "website_url", "about", "views", "up_votes", "down_votes", "category", "branch"},
           [&](std::vector<std::string>& f) {
             authors.insert(std::stoll(f[0]));
             s.write_values(f[0], f[2], f[3]);
           });
      s.complete();
      report.authors = static_cast<std::int64_t>(authors.size());
    }

    {
      auto s = pqxx::stream_to::raw_table(tx, "tags", "name, question_count, category_slug");
      each(directory, "tags", {"name", "question_count", "category_slug"}, [&](std::vector<std::string>& f) {
        s.write_row(Row{f[0], f[1], nullable(f[2])});
        ++report.tags;
      });
      s.complete();
    }
    std::unordered_map<std::string, std::int64_t> tag_ids;
    for (const auto& r : tx.exec("SELECT name, id FROM tags")) tag_ids.emplace(r[0].as<std::string>(), r[1].as<std::int64_t>());

    std::unordered_set<std::int64_t> questions;
    {
      auto q = pqxx::stream_to::raw_table(tx, "questions",
                                          "id, owner_id, created_at, title, score, view_count, answer_count, "
                                          "comment_count, favorite_count, accepted_answer_id, closed_at, tag_names");
      std::vector<std::array<std::string, 5>> links;  // question_id, tag_id, position, score, created_at
      each(directory, "questions",
           {"id", "owner_id", "created_at", "title", "tags", "score", "view_count", "answer_count", "comment_count",
            "favorite_count", "accepted_answer_id", "closed_at"},
           [&](std::vector<std::string>& f) {
             questions.insert(std::stoll(f[0]));
             std::string names;  // the known tags, in order (unknown ones are not linked either)
             std::size_t position = 0;
             std::size_t start = 0;
             while (start < f[4].size()) {
               auto end = f[4].find(' ', start);
               if (end == std::string::npos) end = f[4].size();
               const auto tag = f[4].substr(start, end - start);
               if (auto it = tag_ids.find(tag); it != tag_ids.end()) {
                 links.push_back({f[0], std::to_string(it->second), std::to_string(position++), f[5], f[2]});
                 if (!names.empty()) names += ' ';
                 names += tag;
               }
               start = end + 1;
             }
             q.write_row(Row{f[0], nullable(f[1]), f[2], f[3], f[5], f[6], f[7], f[8], f[9], nullable(f[10]),
                             nullable(f[11]), names});
           });
      q.complete();
      report.questions = static_cast<std::int64_t>(questions.size());
      auto qt = pqxx::stream_to::raw_table(tx, "question_tags", "question_id, tag_id, position, score, created_at");
      for (const auto& link : links) qt.write_values(link[0], link[1], link[2], link[3], link[4]);
      qt.complete();
      report.question_tags = static_cast<std::int64_t>(links.size());
    }

    std::unordered_map<std::int64_t, std::int64_t> answer_question;
    {
      auto s = pqxx::stream_to::raw_table(tx, "answers",
                                          "id, question_id, owner_id, created_at, score, comment_count, is_accepted");
      each(directory, "answers",
           {"id", "question_id", "owner_id", "created_at", "score", "comment_count", "is_accepted"},
           [&](std::vector<std::string>& f) {
             answer_question.emplace(std::stoll(f[0]), std::stoll(f[1]));
             s.write_row(Row{f[0], f[1], nullable(f[2]), f[3], f[4], f[5], f[6]});
           });
      s.complete();
      report.answers = static_cast<std::int64_t>(answer_question.size());
    }

    {
      auto s = pqxx::stream_to::raw_table(tx, "comments", "id, post_id, question_id, user_id, created_at, score, body");
      each(directory, "comments", {"id", "post_id", "user_id", "created_at", "score", "text"},
           [&](std::vector<std::string>& f) {
             const auto post = std::stoll(f[1]);
             std::int64_t question = post;
             if (!questions.contains(post)) {
               auto it = answer_question.find(post);
               if (it == answer_question.end()) return;  // comment on a post outside the dataset
               question = it->second;
             }
             s.write_row(Row{f[0], f[1], std::to_string(question), nullable(f[2]), f[3], f[4], f[5]});
             ++report.comments;
           });
      s.complete();
    }

    {
      auto s = pqxx::stream_to::raw_table(tx, "badges", "id, user_id, name, class, tag_based, awarded_at");
      each(directory, "badges", {"id", "user_id", "name", "class", "tag_based", "awarded_at"},
           [&](std::vector<std::string>& f) {
             if (!authors.contains(std::stoll(f[1]))) return;
             s.write_values(f[0], f[1], f[2], f[3], f[4], f[5]);
             ++report.badges;
           });
      s.complete();
    }

    {
      auto s = pqxx::stream_to::raw_table(tx, "user_tags", "user_id, tag_id, posts, score");
      each(directory, "user_tags", {"user_id", "tag", "posts", "score"}, [&](std::vector<std::string>& f) {
        auto it = tag_ids.find(f[1]);
        if (it == tag_ids.end() || !authors.contains(std::stoll(f[0]))) return;
        s.write_values(f[0], std::to_string(it->second), f[2], f[3]);
        ++report.user_tags;
      });
      s.complete();
    }

    // Accepted answers posted after 2008 are not in the dataset; drop the dangling references so the
    // deferred foreign key holds at commit.
    tx.exec(
        "UPDATE questions q SET accepted_answer_id = NULL WHERE accepted_answer_id IS NOT NULL "
        "AND NOT EXISTS (SELECT 1 FROM answers a WHERE a.id = q.accepted_answer_id)");
    tx.exec("SELECT rebuild_user_activity()");
    tx.exec("ANALYZE");
    report.loaded = true;
  });
  report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  return report;
}

}  // namespace sg::content
