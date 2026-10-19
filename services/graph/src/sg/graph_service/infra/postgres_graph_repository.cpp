#include "sg/graph_service/infra/postgres_graph_repository.hpp"

#include <string>

#include <sg/common/errors.hpp>
#include <sg/common/outbox.hpp>
#include <sg/common/validation.hpp>

namespace sg::graph_service::infra {
namespace {

// Columns read by read_profile(), for a profiles row aliased "p".
constexpr const char* kProfileColumns =
    "p.user_id, p.handle, p.name, p.category, p.branch, p.influence::float8, p.reputation, p.location, "
    "p.website_url, p.about, p.views, p.up_votes, p.down_votes, p.source, "
    "extract(epoch FROM p.created_at)::bigint, extract(epoch FROM p.last_access_at)::bigint";
constexpr int kProfileColumnCount = 16;

constexpr const char* kConnectionCount =
    "((SELECT count(*) FROM connections c WHERE c.user_a = p.user_id) + "
    "(SELECT count(*) FROM connections c WHERE c.user_b = p.user_id))";

// The search filters of GET /users; $1 LIKE pattern for q, $2 category, $3 branch, $4 min_reputation
// (NULL = no filter). The pattern is lower-cased in SQL so the trigram indexes on lower(handle) and
// lower(name) apply.
constexpr const char* kProfileFilters =
    " WHERE ($1::text IS NULL OR lower(p.handle) LIKE lower($1::text) OR lower(p.name) LIKE lower($1::text))"
    " AND ($2::text IS NULL OR p.category = $2::text)"
    " AND ($3::text IS NULL OR p.branch = $3::text)"
    " AND ($4::bigint IS NULL OR p.reputation >= $4::bigint)";

/// `%q%` with LIKE's wildcards and escape character escaped, so q matches literally.
std::optional<std::string> substring_pattern(const std::optional<std::string>& q) {
  if (!q) {
    return std::nullopt;
  }
  std::string pattern = "%";
  for (const char c : *q) {
    if (c == '%' || c == '_' || c == '\\') {
      pattern += '\\';
    }
    pattern += c;
  }
  pattern += '%';
  return pattern;
}

std::optional<std::string> optional_string(const pqxx::field& f) {
  return f.is_null() ? std::nullopt : std::optional<std::string>(f.as<std::string>());
}

std::optional<std::int64_t> optional_int(const pqxx::field& f) {
  return f.is_null() ? std::nullopt : std::optional<std::int64_t>(f.as<std::int64_t>());
}

domain::UserProfile read_profile(const pqxx::row& row, int offset = 0) {
  const auto at = [&](int i) { return row[static_cast<pqxx::row::size_type>(offset + i)]; };
  domain::UserProfile p;
  p.id = at(0).as<std::int64_t>();
  p.handle = at(1).as<std::string>();
  p.name = at(2).as<std::string>();
  p.category = at(3).as<std::string>();
  p.branch = at(4).as<std::string>();
  p.influence = at(5).as<double>();
  p.reputation = at(6).as<std::int64_t>();
  p.location = optional_string(at(7));
  p.website_url = optional_string(at(8));
  p.about = optional_string(at(9));
  p.views = at(10).as<std::int64_t>();
  p.up_votes = at(11).as<std::int64_t>();
  p.down_votes = at(12).as<std::int64_t>();
  p.source = domain::parse_profile_source(at(13).as<std::string>());
  p.created_at = at(14).as<std::int64_t>();
  p.last_access_at = optional_int(at(15));
  return p;
}

std::string order_by(domain::UserSort sort) {
  switch (sort) {
    case domain::UserSort::name:
      return " ORDER BY lower(p.name), p.user_id";
    case domain::UserSort::id:
      return " ORDER BY p.user_id";
    case domain::UserSort::reputation:
      break;
  }
  return " ORDER BY p.reputation DESC, p.user_id";
}

/// Starts a consistent read: every statement of `tx` sees the same snapshot.
void repeatable_read(pqxx::work& tx) { tx.exec("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY"); }

}  // namespace

std::int64_t lock_graph_version(pqxx::work& tx) {
  return tx.query_value<std::int64_t>("SELECT version FROM graph_meta WHERE id FOR UPDATE");
}

std::int64_t bump_graph_version(pqxx::work& tx) {
  return tx.query_value<std::int64_t>("UPDATE graph_meta SET version = version + 1 WHERE id RETURNING version");
}

domain::Taxonomy PostgresGraphRepository::load_taxonomy() {
  return pool_.with_transaction([](pqxx::work& tx) {
    repeatable_read(tx);
    domain::Taxonomy t;
    for (const auto& row : tx.exec("SELECT slug, name, description FROM categories ORDER BY id")) {
      t.categories.push_back({row[0].as<std::string>(), row[1].as<std::string>(), row[2].as<std::string>()});
    }
    for (const auto& row : tx.exec("SELECT name, category_slug FROM branches ORDER BY id")) {
      t.branches.push_back({row[0].as<std::string>(), row[1].as<std::string>()});
    }
    return t;
  });
}

std::optional<domain::ProfileWithCount> PostgresGraphRepository::find_profile(std::int64_t user_id) {
  return pool_.with_transaction([&](pqxx::work& tx) -> std::optional<domain::ProfileWithCount> {
    const auto result = tx.exec_params(std::string("SELECT ") + kProfileColumns + ", " + kConnectionCount +
                                           " FROM profiles p WHERE p.user_id = $1",
                                       user_id);
    if (result.empty()) {
      return std::nullopt;
    }
    return domain::ProfileWithCount{read_profile(result[0]), result[0][kProfileColumnCount].as<std::int64_t>()};
  });
}

domain::UserPage PostgresGraphRepository::list_profiles(const domain::UserQuery& query) {
  return pool_.with_transaction([&](pqxx::work& tx) {
    repeatable_read(tx);
    domain::UserPage page;
    page.limit = query.limit;
    page.offset = query.offset;
    const auto pattern = substring_pattern(query.q);
    page.total = tx.exec_params1(std::string("SELECT count(*) FROM profiles p") + kProfileFilters, pattern,
                                 query.category, query.branch, query.min_reputation)[0]
                     .as<std::int64_t>();
    const auto rows = tx.exec_params(std::string("SELECT ") + kProfileColumns + ", " + kConnectionCount +
                                         " FROM profiles p" + kProfileFilters + order_by(query.sort) +
                                         " LIMIT $5 OFFSET $6",
                                     pattern, query.category, query.branch, query.min_reputation, query.limit,
                                     query.offset);
    page.items.reserve(rows.size());
    for (const auto& row : rows) {
      page.items.push_back({read_profile(row), row[kProfileColumnCount].as<std::int64_t>()});
    }
    return page;
  });
}

domain::ConnectionList PostgresGraphRepository::list_connections(std::int64_t user_id) {
  return pool_.with_transaction([&](pqxx::work& tx) {
    repeatable_read(tx);
    domain::ConnectionList list;
    list.graph_version = tx.query_value<std::int64_t>("SELECT version FROM graph_meta WHERE id");
    const auto rows = tx.exec_params(
        std::string("SELECT ") + kProfileColumns +
            ", c.strength::float8, c.interactions, c.origin, extract(epoch FROM c.created_at)::bigint,"
            " extract(epoch FROM c.first_interaction_at)::bigint, extract(epoch FROM c.last_interaction_at)::bigint"
            " FROM connections c"
            " JOIN profiles p ON p.user_id = CASE WHEN c.user_a = $1 THEN c.user_b ELSE c.user_a END"
            " WHERE c.user_a = $1 OR c.user_b = $1"
            " ORDER BY c.strength DESC, p.user_id",
        user_id);
    list.items.reserve(rows.size());
    for (const auto& row : rows) {
      domain::ConnectionItem item;
      item.user = read_profile(row);
      item.strength = sg::common::round3(row[kProfileColumnCount].as<double>());
      item.interactions = row[kProfileColumnCount + 1].as<std::int64_t>();
      item.origin = domain::parse_connection_origin(row[kProfileColumnCount + 2].as<std::string>());
      item.connected_at = row[kProfileColumnCount + 3].as<std::int64_t>();
      item.first_interaction_at = optional_int(row[kProfileColumnCount + 4]);
      item.last_interaction_at = optional_int(row[kProfileColumnCount + 5]);
      list.items.push_back(std::move(item));
    }
    return list;
  });
}

domain::AddedConnection PostgresGraphRepository::add_connection(std::int64_t user_id, std::int64_t other_id,
                                                                double strength) {
  const auto [a, b] = domain::ordered(user_id, other_id);
  const double rounded = sg::common::round3(strength);
  return pool_.with_transaction([&](pqxx::work& tx) {
    (void)lock_graph_version(tx);
    const auto profiles = tx.exec_params(std::string("SELECT ") + kProfileColumns +
                                             " FROM profiles p WHERE p.user_id = $1 OR p.user_id = $2",
                                         user_id, other_id);
    std::optional<domain::UserProfile> other;
    bool caller_exists = false;
    for (const auto& row : profiles) {
      auto profile = read_profile(row);
      if (profile.id == user_id) {
        caller_exists = true;
      } else {
        other = std::move(profile);
      }
    }
    if (!caller_exists) {
      throw sg::common::not_found("Profile is still being provisioned, retry shortly");
    }
    if (!other) {
      throw sg::common::not_found("User not found");
    }
    const auto inserted = tx.exec_params(
        "INSERT INTO connections (user_a, user_b, strength, interactions, origin) VALUES ($1, $2, $3, 0, 'member') "
        "ON CONFLICT (user_a, user_b) DO NOTHING RETURNING extract(epoch FROM created_at)::bigint",
        a, b, rounded);
    if (inserted.empty()) {
      throw sg::common::conflict("Already connected");
    }
    const std::int64_t version = bump_graph_version(tx);
    sg::common::enqueue_event(tx, "connection.added", domain::connection_added_payload(version, a, b, rounded));

    domain::AddedConnection added;
    added.graph_version = version;
    added.connection.user = std::move(*other);
    added.connection.strength = rounded;
    added.connection.interactions = 0;
    added.connection.origin = domain::ConnectionOrigin::member;
    added.connection.connected_at = inserted[0][0].as<std::int64_t>();
    return added;
  });
}

std::int64_t PostgresGraphRepository::remove_connection(std::int64_t user_id, std::int64_t other_id) {
  const auto [a, b] = domain::ordered(user_id, other_id);
  return pool_.with_transaction([&](pqxx::work& tx) {
    (void)lock_graph_version(tx);
    const auto deleted =
        tx.exec_params("DELETE FROM connections WHERE user_a = $1 AND user_b = $2 RETURNING user_a", a, b);
    if (deleted.empty()) {
      throw sg::common::not_found("Not connected to this user");
    }
    const std::int64_t version = bump_graph_version(tx);
    sg::common::enqueue_event(tx, "connection.removed", domain::connection_removed_payload(version, a, b));
    return version;
  });
}

bool PostgresGraphRepository::create_member_profile(const domain::NewMember& member, const std::string& stream,
                                                    const std::string& event_id) {
  try {
    return pool_.with_transaction([&](pqxx::work& tx) {
      (void)lock_graph_version(tx);
      if (tx.exec_params("INSERT INTO processed_events (stream, event_id) VALUES ($1, $2) "
                         "ON CONFLICT DO NOTHING RETURNING event_id",
                         stream, event_id)
              .empty()) {
        return false;  // redelivered event
      }
      if (tx.exec_params1("SELECT EXISTS (SELECT 1 FROM profiles WHERE user_id = $1)", member.user_id)[0].as<bool>()) {
        return false;  // same registration published twice under different outbox ids
      }
      const std::int64_t version = bump_graph_version(tx);
      const auto created = tx.exec_params(
          "INSERT INTO profiles (user_id, handle, name, category, branch, influence, reputation, location, about, source) "
          "VALUES ($1, $2, $3, $4, $5, 0, 1, $6, $7, 'member') RETURNING extract(epoch FROM created_at)::bigint",
          member.user_id, member.handle, member.name, member.category, member.branch, member.location, member.about);
      const auto profile = domain::member_profile(member, created[0][0].as<std::int64_t>());
      sg::common::enqueue_event(tx, "user.created", domain::user_created_payload(version, profile));
      return true;
    });
  } catch (const pqxx::integrity_constraint_violation& e) {
    throw app::PoisonEvent(std::string("user.registered rejected by the schema: ") + e.what());
  } catch (const pqxx::data_exception& e) {
    throw app::PoisonEvent(std::string("user.registered rejected by the schema: ") + e.what());
  }
}

void PostgresGraphRepository::record_dead_letter(const std::string& stream, const std::string& entry_id,
                                                 const nlohmann::json& fields, const std::string& error) {
  const std::string fields_text = fields.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
  pool_.with_transaction([&](pqxx::work& tx) {
    tx.exec_params("INSERT INTO dead_letter_events (stream, entry_id, fields, error) VALUES ($1, $2, $3::jsonb, $4)",
                   stream, entry_id, fields_text, error);
  });
}

domain::Snapshot PostgresGraphRepository::snapshot() {
  return pool_.with_transaction([](pqxx::work& tx) {
    repeatable_read(tx);
    tx.exec("SET LOCAL statement_timeout = '60s'");
    domain::Snapshot s;
    s.graph_version = tx.query_value<std::int64_t>("SELECT version FROM graph_meta WHERE id");
    const auto users = tx.exec(std::string("SELECT ") + kProfileColumns + " FROM profiles p ORDER BY p.user_id");
    s.users.reserve(users.size());
    for (const auto& row : users) {
      s.users.push_back(read_profile(row));
    }
    const auto edges = tx.exec("SELECT user_a, user_b, strength::float8 FROM connections ORDER BY user_a, user_b");
    s.edges.reserve(edges.size());
    for (const auto& row : edges) {
      s.edges.push_back({row[0].as<std::int64_t>(), row[1].as<std::int64_t>(), sg::common::round3(row[2].as<double>())});
    }
    return s;
  });
}

std::int64_t PostgresGraphRepository::graph_version() {
  return pool_.with_transaction(
      [](pqxx::work& tx) { return tx.query_value<std::int64_t>("SELECT version FROM graph_meta WHERE id"); });
}

}  // namespace sg::graph_service::infra
