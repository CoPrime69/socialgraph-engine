#include "sg/graph_service/infra/postgres_graph_repository.hpp"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <future>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

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
  return pool_.with_read([&](pqxx::nontransaction& tx) -> std::optional<domain::ProfileWithCount> {
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

// ---------------------------------------------------------------------------------------------
// Group commit

class PostgresGraphRepository::GroupCommitWriter {
 public:
  struct Write {
    bool add = true;
    std::int64_t a = 0;  // a < b
    std::int64_t b = 0;
    double strength = 0.0;
  };
  struct Outcome {
    bool changed = false;            // inserted / deleted; false: already connected / not connected
    std::int64_t graph_version = 0;  // the version of this change (when changed)
    std::int64_t connected_at = 0;   // added connections only
  };

  GroupCommitWriter(sg::common::PgPool& pool, std::size_t max_batch)
      : pool_(pool), max_batch_(std::max<std::size_t>(max_batch, 1)), thread_([this](const std::stop_token& stop) { run(stop); }) {}

  ~GroupCommitWriter() {
    thread_.request_stop();
    {
      std::lock_guard lock(mutex_);  // pairs with the wait predicate
    }
    wake_.notify_all();
    thread_.join();
  }

  GroupCommitWriter(const GroupCommitWriter&) = delete;
  GroupCommitWriter& operator=(const GroupCommitWriter&) = delete;

  /// Blocks until the batch holding `write` committed (or failed: rethrows its error).
  Outcome submit(const Write& write) {
    Pending pending{write, {}};
    auto done = pending.promise.get_future();
    {
      std::lock_guard lock(mutex_);
      queue_.push_back(&pending);
    }
    wake_.notify_one();
    return done.get();
  }

 private:
  struct Pending {
    Write write;
    std::promise<Outcome> promise;
  };

  void run(const std::stop_token& stop) {
    std::vector<Pending*> batch;
    std::set<std::pair<std::int64_t, std::int64_t>> pairs;
    for (;;) {
      {
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [&] { return stop.stop_requested() || !queue_.empty(); });
        if (queue_.empty()) {
          return;  // stopping, nothing left to do
        }
        // At most one write per pair in a batch, so the set-based statements below need no
        // ordering within the batch; a second write to the same pair waits for the next batch.
        for (auto it = queue_.begin(); it != queue_.end() && batch.size() < max_batch_;) {
          if (pairs.emplace((*it)->write.a, (*it)->write.b).second) {
            batch.push_back(*it);
            it = queue_.erase(it);
          } else {
            ++it;
          }
        }
      }
      execute(batch);
      batch.clear();
      pairs.clear();
    }
  }

  void execute(const std::vector<Pending*>& batch) {
    std::vector<Outcome> outcomes;
    try {
      pool_.with_transaction([&](pqxx::work& tx) {
        outcomes.assign(batch.size(), Outcome{});
        std::vector<std::int64_t> add_a, add_b, remove_a, remove_b;
        std::vector<double> add_strength;
        for (const auto* p : batch) {
          if (p->write.add) {
            add_a.push_back(p->write.a);
            add_b.push_back(p->write.b);
            add_strength.push_back(p->write.strength);
          } else {
            remove_a.push_back(p->write.a);
            remove_b.push_back(p->write.b);
          }
        }
        const std::int64_t locked = lock_graph_version(tx);
        // One statement per kind of write, whatever the batch size: already-connected pairs are
        // skipped by ON CONFLICT and missing pairs simply return no row.
        std::map<std::pair<std::int64_t, std::int64_t>, std::int64_t> added;  // pair -> connected_at
        if (!add_a.empty()) {
          for (const auto& row : tx.exec_params(
                   "INSERT INTO connections (user_a, user_b, strength, interactions, origin) "
                   "SELECT a, b, s, 0, 'member' FROM unnest($1::bigint[], $2::bigint[], $3::float8[]) AS w(a, b, s) "
                   "ON CONFLICT (user_a, user_b) DO NOTHING "
                   "RETURNING user_a, user_b, extract(epoch FROM created_at)::bigint",
                   add_a, add_b, add_strength)) {
            added.emplace(std::pair{row[0].as<std::int64_t>(), row[1].as<std::int64_t>()}, row[2].as<std::int64_t>());
          }
        }
        std::set<std::pair<std::int64_t, std::int64_t>> removed;
        if (!remove_a.empty()) {
          for (const auto& row : tx.exec_params(
                   "DELETE FROM connections c USING unnest($1::bigint[], $2::bigint[]) AS w(a, b) "
                   "WHERE c.user_a = w.a AND c.user_b = w.b RETURNING c.user_a, c.user_b",
                   remove_a, remove_b)) {
            removed.emplace(row[0].as<std::int64_t>(), row[1].as<std::int64_t>());
          }
        }
        // Versions and events follow the order the writes arrived in.
        std::int64_t version = locked;
        std::vector<std::pair<std::string, nlohmann::json>> events;
        for (std::size_t i = 0; i < batch.size(); ++i) {
          const Write& w = batch[i]->write;
          Outcome& out = outcomes[i];
          if (w.add) {
            const auto it = added.find({w.a, w.b});
            if (it == added.end()) {
              continue;
            }
            out.connected_at = it->second;
            out.graph_version = ++version;
            events.emplace_back("connection.added", domain::connection_added_payload(version, w.a, w.b, w.strength));
          } else {
            if (!removed.contains({w.a, w.b})) {
              continue;
            }
            out.graph_version = ++version;
            events.emplace_back("connection.removed", domain::connection_removed_payload(version, w.a, w.b));
          }
          out.changed = true;
        }
        if (version != locked) {
          sg::common::enqueue_events(tx, events);
          tx.exec_params("UPDATE graph_meta SET version = $1 WHERE id", version);
        }
      });
    } catch (...) {
      const auto error = std::current_exception();
      for (auto* p : batch) {
        p->promise.set_exception(error);
      }
      return;
    }
    for (std::size_t i = 0; i < batch.size(); ++i) {
      batch[i]->promise.set_value(outcomes[i]);
    }
  }

  sg::common::PgPool& pool_;
  const std::size_t max_batch_;
  std::mutex mutex_;
  std::condition_variable_any wake_;
  std::list<Pending*> queue_;
  std::jthread thread_;  // last: started after everything it uses
};

PostgresGraphRepository::PostgresGraphRepository(sg::common::PgPool& pool, Options options)
    : pool_(pool), writer_(std::make_unique<GroupCommitWriter>(pool, options.max_batch)) {}

PostgresGraphRepository::~PostgresGraphRepository() = default;

domain::AddedConnection PostgresGraphRepository::add_connection(std::int64_t user_id, std::int64_t other_id,
                                                                double strength) {
  const auto [a, b] = domain::ordered(user_id, other_id);
  const double rounded = sg::common::round3(strength);
  // Profiles are never deleted, so checking them before the (serialised) write is safe and keeps
  // the batch free of failing statements.
  std::optional<domain::UserProfile> other;
  bool caller_exists = false;
  pool_.with_read([&](pqxx::nontransaction& tx) {
    for (const auto& row : tx.exec_params(std::string("SELECT ") + kProfileColumns +
                                               " FROM profiles p WHERE p.user_id = $1 OR p.user_id = $2",
                                           user_id, other_id)) {
      auto profile = read_profile(row);
      if (profile.id == user_id) {
        caller_exists = true;
      } else {
        other = std::move(profile);
      }
    }
  });
  if (!caller_exists) {
    throw sg::common::not_found("Profile is still being provisioned, retry shortly");
  }
  if (!other) {
    throw sg::common::not_found("User not found");
  }
  const auto outcome = writer_->submit({true, a, b, rounded});
  if (!outcome.changed) {
    throw sg::common::conflict("Already connected");
  }
  domain::AddedConnection added;
  added.graph_version = outcome.graph_version;
  added.connection.user = std::move(*other);
  added.connection.strength = rounded;
  added.connection.interactions = 0;
  added.connection.origin = domain::ConnectionOrigin::member;
  added.connection.connected_at = outcome.connected_at;
  return added;
}

std::int64_t PostgresGraphRepository::remove_connection(std::int64_t user_id, std::int64_t other_id) {
  const auto [a, b] = domain::ordered(user_id, other_id);
  const auto outcome = writer_->submit({false, a, b, 0.0});
  if (!outcome.changed) {
    throw sg::common::not_found("Not connected to this user");
  }
  return outcome.graph_version;
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
