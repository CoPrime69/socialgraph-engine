#include "sg/graph_service/infra/seed_loader.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <sg/common/csv.hpp>
#include <sg/common/log.hpp>
#include <sg/common/time.hpp>
#include <sg/common/validation.hpp>

#include "sg/graph_service/domain/affinity.hpp"
#include "sg/graph_service/domain/profile.hpp"
#include "sg/graph_service/domain/taxonomy.hpp"
#include "sg/graph_service/infra/migrations.hpp"
#include "sg/graph_service/infra/postgres_graph_repository.hpp"

namespace sg::graph_service::infra {
namespace {

namespace fs = std::filesystem;
using sg::common::CsvError;
using sg::common::CsvReader;
using sg::common::CsvRecord;

/// Throws a CsvError naming the file and line of `record`.
[[noreturn]] void fail(const CsvReader& reader, const CsvRecord& record, const std::string& message) {
  throw CsvError(record.line, message, reader.path().filename().string());
}

void expect_fields(const CsvReader& reader, const CsvRecord& record, std::size_t count) {
  if (record.fields.size() != count) {
    fail(reader, record, "expected " + std::to_string(count) + " fields, found " + std::to_string(record.fields.size()));
  }
}

std::int64_t parse_int(const CsvReader& reader, const CsvRecord& record, std::size_t index, const char* column,
                       std::int64_t min, std::int64_t max) {
  const std::string& text = record.fields[index];
  std::int64_t value = 0;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || ec != std::errc() || end != text.data() + text.size() || value < min || value > max) {
    fail(reader, record,
         std::string(column) + " must be an integer between " + std::to_string(min) + " and " + std::to_string(max));
  }
  return value;
}

double parse_number(const CsvReader& reader, const CsvRecord& record, std::size_t index, const char* column,
                    double min, double max) {
  const std::string& text = record.fields[index];
  double value = 0.0;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || ec != std::errc() || end != text.data() + text.size() || !(value >= min && value <= max)) {
    fail(reader, record, std::string(column) + " must be a number between " + std::to_string(min) + " and " +
                             std::to_string(max));
  }
  return value;
}

/// "YYYY-MM-DDTHH:MM:SS[.fraction]Z" with plausible field ranges.
bool is_rfc3339_utc(std::string_view t) noexcept {
  const auto digits = [&](std::size_t pos, std::size_t n, int min, int max) {
    if (pos + n > t.size()) {
      return false;
    }
    int value = 0;
    for (std::size_t i = pos; i < pos + n; ++i) {
      if (t[i] < '0' || t[i] > '9') {
        return false;
      }
      value = value * 10 + (t[i] - '0');
    }
    return value >= min && value <= max;
  };
  if (t.size() < 20 || t[4] != '-' || t[7] != '-' || t[10] != 'T' || t[13] != ':' || t[16] != ':' || t.back() != 'Z') {
    return false;
  }
  if (!digits(0, 4, 0, 9999) || !digits(5, 2, 1, 12) || !digits(8, 2, 1, 31) || !digits(11, 2, 0, 23) ||
      !digits(14, 2, 0, 59) || !digits(17, 2, 0, 60)) {
    return false;
  }
  if (t.size() == 20) {
    return true;
  }
  // Optional fraction: '.' followed by 1-9 digits, then 'Z'.
  if (t[19] != '.' || t.size() < 22 || t.size() > 30) {
    return false;
  }
  return std::all_of(t.begin() + 20, t.end() - 1, [](char c) { return c >= '0' && c <= '9'; });
}

/// RFC 3339 UTC timestamp; empty -> NULL unless required.
std::optional<std::string> parse_timestamp(const CsvReader& reader, const CsvRecord& record, std::size_t index,
                                           const char* column, bool required) {
  const std::string& text = record.fields[index];
  if (text.empty()) {
    if (required) {
      fail(reader, record, std::string(column) + " is required");
    }
    return std::nullopt;
  }
  if (!is_rfc3339_utc(text)) {
    fail(reader, record, std::string(column) + " must be an RFC 3339 UTC timestamp");
  }
  return text;
}

/// Free text: empty -> NULL; otherwise valid UTF-8 without control characters, at most `max` characters.
std::optional<std::string> parse_text(const CsvReader& reader, const CsvRecord& record, std::size_t index,
                                      const char* column, std::size_t max, bool allow_newlines = false) {
  const std::string& text = record.fields[index];
  if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
    return std::nullopt;
  }
  auto normalized = sg::common::normalize_text(text, max, allow_newlines);
  if (!normalized) {
    fail(reader, record, std::string(column) + " must be at most " + std::to_string(max) +
                             " characters without control characters");
  }
  return normalized;
}

std::string required_text(const CsvReader& reader, const CsvRecord& record, std::size_t index, const char* column,
                          std::size_t max) {
  auto text = parse_text(reader, record, index, column, max);
  if (!text) {
    fail(reader, record, std::string(column) + " is required");
  }
  return std::move(*text);
}

domain::Taxonomy read_taxonomy(const fs::path& categories_file, const fs::path& branches_file) {
  const auto valid_slug = [](const std::string& slug) {
    const auto ok = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    return !slug.empty() && slug.size() <= 64 && ok(slug[0]) &&
           std::all_of(slug.begin(), slug.end(), [&](char c) { return ok(c) || c == '_' || c == '-'; });
  };
  domain::Taxonomy taxonomy;
  {
    CsvReader reader(categories_file);
    reader.expect_header({"slug", "name", "description"});
    std::unordered_set<std::string> slugs;
    std::unordered_set<std::string> names;
    while (auto record = reader.next()) {
      expect_fields(reader, *record, 3);
      domain::Category c;
      c.slug = record->fields[0];
      if (!valid_slug(c.slug)) {
        fail(reader, *record, "slug must match [a-z0-9][a-z0-9_-]{0,63}");
      }
      c.name = required_text(reader, *record, 1, "name", 64);
      c.description = parse_text(reader, *record, 2, "description", 500).value_or("");
      if (!slugs.insert(c.slug).second || !names.insert(c.name).second) {
        fail(reader, *record, "duplicate category '" + c.slug + "'");
      }
      taxonomy.categories.push_back(std::move(c));
    }
  }
  {
    CsvReader reader(branches_file);
    reader.expect_header({"name", "category_slug"});
    std::unordered_set<std::string> names;
    while (auto record = reader.next()) {
      expect_fields(reader, *record, 2);
      domain::Branch b;
      b.name = required_text(reader, *record, 0, "name", 64);
      b.category_slug = record->fields[1];
      if (!taxonomy.category_name_by_slug(b.category_slug)) {
        fail(reader, *record, "unknown category_slug '" + b.category_slug + "'");
      }
      if (!names.insert(b.name).second) {
        fail(reader, *record, "duplicate branch '" + b.name + "'");
      }
      taxonomy.branches.push_back(std::move(b));
    }
  }
  if (taxonomy.empty()) {
    throw std::runtime_error("dataset taxonomy is empty (" + categories_file.string() + ", " + branches_file.string() + ")");
  }
  return taxonomy;
}

void insert_taxonomy(pqxx::work& tx, const domain::Taxonomy& taxonomy) {
  int id = 0;
  for (const auto& c : taxonomy.categories) {
    tx.exec_params("INSERT INTO categories (id, slug, name, description) VALUES ($1, $2, $3, $4)", ++id, c.slug, c.name,
                   c.description);
  }
  id = 0;
  for (const auto& b : taxonomy.branches) {
    tx.exec_params("INSERT INTO branches (id, name, category_slug) VALUES ($1, $2, $3)", ++id, b.name, b.category_slug);
  }
}

/// What the demo seeding needs to know about an imported user.
struct Candidate {
  std::int64_t id;
  std::int64_t reputation;
  domain::UserProfile profile;
};

struct ImportedUsers {
  std::unordered_set<std::int64_t> ids;
  std::vector<Candidate> demo_candidates;  ///< users of the demo member's branch
};

ImportedUsers import_users(pqxx::work& tx, const fs::path& file, const domain::Taxonomy& taxonomy) {
  CsvReader reader(file);
  reader.expect_header({"id", "handle", "display_name", "reputation", "influence", "created_at", "last_access_at",
                        "location", "website_url", "about", "views", "up_votes", "down_votes", "category", "branch"});
  ImportedUsers out;
  std::unordered_set<std::string> handles;
  auto stream = pqxx::stream_to::table(tx, {"profiles"},
                                       {"user_id", "handle", "name", "category", "branch", "influence", "reputation",
                                        "location", "website_url", "about", "views", "up_votes", "down_votes", "source",
                                        "created_at", "last_access_at"});
  constexpr std::int64_t kMaxInt32 = 2'147'483'647;
  while (auto r = reader.next()) {
    expect_fields(reader, *r, 15);
    const std::int64_t id = parse_int(reader, *r, 0, "id", 1, domain::kFirstMemberId - 1);
    const std::string& handle = r->fields[1];
    if (!sg::common::is_valid_handle(handle)) {
      fail(reader, *r, "handle must match ^[a-z0-9_]{3,32}$");
    }
    auto name = sg::common::normalize_name(r->fields[2]);
    if (!name) {
      fail(reader, *r, "display_name must be 1-64 characters without control characters");
    }
    const std::int64_t reputation = parse_int(reader, *r, 3, "reputation", 0, kMaxInt32);
    const double influence = sg::common::round_influence(parse_number(reader, *r, 4, "influence", 0.0, 100.0));
    const auto created_at = parse_timestamp(reader, *r, 5, "created_at", true);
    const auto last_access_at = parse_timestamp(reader, *r, 6, "last_access_at", false);
    const auto location = parse_text(reader, *r, 7, "location", 100);
    const auto website_url = parse_text(reader, *r, 8, "website_url", 300);
    const auto about = parse_text(reader, *r, 9, "about", 500, true);
    const std::int64_t views = parse_int(reader, *r, 10, "views", 0, kMaxInt32);
    const std::int64_t up_votes = parse_int(reader, *r, 11, "up_votes", 0, kMaxInt32);
    const std::int64_t down_votes = parse_int(reader, *r, 12, "down_votes", 0, kMaxInt32);
    const std::string& category = r->fields[13];
    const std::string& branch = r->fields[14];
    if (!taxonomy.has_category(category)) {
      fail(reader, *r, "unknown category '" + category + "'");
    }
    if (!taxonomy.has_branch(branch)) {
      fail(reader, *r, "unknown branch '" + branch + "'");
    }
    if (!out.ids.insert(id).second) {
      fail(reader, *r, "duplicate id " + std::to_string(id));
    }
    if (!handles.insert(handle).second) {
      fail(reader, *r, "duplicate handle '" + handle + "'");
    }
    stream.write_values(id, handle, *name, category, branch, influence, reputation, location, website_url, about, views,
                        up_votes, down_votes, std::string_view("stackoverflow"), *created_at, last_access_at);
    if (branch == kDemoBranch) {
      domain::UserProfile p;
      p.id = id;
      p.category = category;
      p.branch = branch;
      p.influence = influence;
      out.demo_candidates.push_back({id, reputation, std::move(p)});
    }
  }
  stream.complete();
  return out;
}

std::size_t import_connections(pqxx::work& tx, const fs::path& file, const std::unordered_set<std::int64_t>& ids,
                               const std::string& now) {
  CsvReader reader(file);
  reader.expect_header({"user_a", "user_b", "interactions", "first_interaction_at", "last_interaction_at", "strength"});
  struct PairHash {
    std::size_t operator()(const std::pair<std::int64_t, std::int64_t>& p) const noexcept {
      return std::hash<std::int64_t>{}(p.first * 1'000'003 ^ p.second);
    }
  };
  std::unordered_set<std::pair<std::int64_t, std::int64_t>, PairHash> seen;
  seen.reserve(256 * 1024);
  auto stream = pqxx::stream_to::table(tx, {"connections"},
                                       {"user_a", "user_b", "strength", "interactions", "origin", "created_at",
                                        "first_interaction_at", "last_interaction_at"});
  std::size_t count = 0;
  while (auto r = reader.next()) {
    expect_fields(reader, *r, 6);
    const std::int64_t a = parse_int(reader, *r, 0, "user_a", 1, domain::kFirstMemberId - 1);
    const std::int64_t b = parse_int(reader, *r, 1, "user_b", 1, domain::kFirstMemberId - 1);
    if (a >= b) {
      fail(reader, *r, "user_a must be less than user_b");
    }
    if (!ids.contains(a) || !ids.contains(b)) {
      fail(reader, *r, "connection references an unknown user");
    }
    if (!seen.insert({a, b}).second) {
      fail(reader, *r, "duplicate connection");
    }
    const std::int64_t interactions = parse_int(reader, *r, 2, "interactions", 0, 2'147'483'647);
    const auto first = parse_timestamp(reader, *r, 3, "first_interaction_at", false);
    const auto last = parse_timestamp(reader, *r, 4, "last_interaction_at", false);
    const double strength = sg::common::round3(parse_number(reader, *r, 5, "strength", 0.0, 1.0));
    const std::string& connected_at = first ? *first : (last ? *last : now);
    stream.write_values(a, b, strength, interactions, std::string_view("imported"), connected_at, first, last);
    ++count;
  }
  stream.complete();
  return count;
}

/// The demo member (contracts §10.2) and its connections to the most reputable users of its branch.
std::size_t seed_demo_member(pqxx::work& tx, const domain::Taxonomy& taxonomy, std::vector<Candidate> candidates) {
  if (!taxonomy.has_category(kDemoCategory) || !taxonomy.has_branch(kDemoBranch)) {
    throw std::runtime_error(std::string("dataset has no category '") + kDemoCategory + "' or branch '" + kDemoBranch +
                             "' for the demo member");
  }
  tx.exec_params(
      "INSERT INTO profiles (user_id, handle, name, category, branch, influence, reputation, source) "
      "VALUES ($1, $2, $3, $4, $5, 0, 1, 'member')",
      kDemoUserId, std::string(kDemoHandle), std::string(kDemoName), std::string(kDemoCategory),
      std::string(kDemoBranch));
  domain::UserProfile demo;
  demo.id = kDemoUserId;
  demo.category = kDemoCategory;
  demo.branch = kDemoBranch;
  demo.influence = 0.0;

  const std::size_t n = std::min(kDemoConnections, candidates.size());
  std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(n), candidates.end(),
                    [](const Candidate& x, const Candidate& y) {
                      return x.reputation != y.reputation ? x.reputation > y.reputation : x.id < y.id;
                    });
  for (std::size_t i = 0; i < n; ++i) {
    const double strength = domain::member_connection_strength(demo, candidates[i].profile, taxonomy);
    const auto [a, b] = domain::ordered(kDemoUserId, candidates[i].id);
    tx.exec_params("INSERT INTO connections (user_a, user_b, strength, interactions, origin) VALUES ($1, $2, $3, 0, 'member')",
                   a, b, strength);
  }
  return n;
}

}  // namespace

std::filesystem::path dataset_file(const std::filesystem::path& dir, std::string_view name) {
  const auto gz = dir / (std::string(name) + ".csv.gz");
  if (fs::exists(gz)) {
    return gz;
  }
  const auto plain = dir / (std::string(name) + ".csv");
  if (fs::exists(plain)) {
    return plain;
  }
  throw std::runtime_error("dataset file not found: " + gz.string() + " (or " + plain.filename().string() + ")");
}

SeedReport seed_dataset_if_empty(sg::common::PgPool& pool, const std::filesystem::path& dir) {
  const auto started = std::chrono::steady_clock::now();
  const bool empty = pool.with_transaction(
      [](pqxx::work& tx) { return !tx.query_value<bool>("SELECT EXISTS (SELECT 1 FROM categories)"); });
  if (!empty) {
    return {};
  }
  const auto categories_file = dataset_file(dir, "categories");
  const auto branches_file = dataset_file(dir, "branches");
  const auto users_file = dataset_file(dir, "users");
  const auto connections_file = dataset_file(dir, "connections");
  const domain::Taxonomy taxonomy = read_taxonomy(categories_file, branches_file);
  const std::string now = sg::common::to_rfc3339(std::chrono::system_clock::now());

  SeedReport report = pool.with_transaction([&](pqxx::work& tx) {
    SeedReport r;
    tx.exec("SET LOCAL statement_timeout = 0");
    tx.exec_params("SELECT pg_advisory_xact_lock($1)", kSeedLockKey);
    (void)lock_graph_version(tx);
    if (tx.query_value<bool>("SELECT EXISTS (SELECT 1 FROM categories)")) {
      return r;  // another instance seeded meanwhile
    }
    insert_taxonomy(tx, taxonomy);
    auto users = import_users(tx, users_file, taxonomy);
    r.connections = import_connections(tx, connections_file, users.ids, now);
    r.users = users.ids.size();
    r.demo_connections = seed_demo_member(tx, taxonomy, std::move(users.demo_candidates));
    r.graph_version = bump_graph_version(tx);
    for (const char* table : {"categories", "branches", "profiles", "connections"}) {
      tx.exec(std::string("ANALYZE ") + table);
    }
    r.loaded = true;
    r.categories = taxonomy.categories.size();
    r.branches = taxonomy.branches.size();
    return r;
  });
  // VACUUM cannot run inside a transaction. It sets the visibility map so index-only scans skip the
  // heap right away, instead of after autovacuum gets round to the freshly imported tables.
  sg::common::vacuum_analyze(pool, "profiles, connections");
  report.seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  return report;
}

}  // namespace sg::graph_service::infra
