#include "sg/graph_service/http/routes.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include <sg/common/errors.hpp>
#include <sg/common/validation.hpp>

#include "sg/graph_service/domain/events.hpp"

namespace sg::graph_service::http {
namespace {

using nlohmann::json;
using sg::common::Request;
using sg::common::Response;

/// A non-empty query parameter, trimmed of ASCII whitespace.
std::optional<std::string> text_param(const Request& req, const char* name) {
  auto value = sg::common::query_param(req, name);
  if (!value) {
    return std::nullopt;
  }
  const auto first = value->find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return std::nullopt;
  }
  const auto last = value->find_last_not_of(" \t\r\n");
  return value->substr(first, last - first + 1);
}

std::string render(const json& body) { return body.dump(-1, ' ', false, json::error_handler_t::replace); }

void write_cached(Response& res, const std::shared_ptr<const std::string>& body) {
  res.status = 200;
  res.set_content(*body, "application/json; charset=utf-8");
}

/// Cache key of a validated search: every field that changes the result.
std::string search_key(const domain::UserQuery& q) {
  std::string key;
  const auto add = [&key](const std::optional<std::string>& v) {
    key += v ? std::to_string(v->size()) + ":" + *v : std::string("-");
    key += '|';
  };
  add(q.q);
  add(q.category);
  add(q.branch);
  key += (q.min_reputation ? std::to_string(*q.min_reputation) : std::string("-")) + '|';
  key += std::to_string(static_cast<int>(q.sort)) + '|' + std::to_string(q.limit) + '|' + std::to_string(q.offset);
  return key;
}

/// `user_id` of POST /me/connections: 400 for a wrong type, 422 for a missing or non-positive value.
std::int64_t target_user_id(const json& body) {
  const auto it = body.find("user_id");
  if (it == body.end() || it->is_null()) {
    throw sg::common::validation_failed({{"user_id", "is required"}});
  }
  if (!it->is_number_integer()) {
    throw sg::common::bad_request("Field 'user_id' must be an integer");
  }
  if (it->is_number_unsigned() && it->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    throw sg::common::validation_failed({{"user_id", "must be a positive 64-bit integer"}});
  }
  const auto id = it->get<std::int64_t>();
  if (id <= 0) {
    throw sg::common::validation_failed({{"user_id", "must be a positive 64-bit integer"}});
  }
  return id;
}

}  // namespace

domain::UserQuery parse_user_query(const Request& req) {
  domain::UserQuery query;
  if (auto q = text_param(req, "q")) {
    if (!sg::common::is_valid_utf8(*q) || sg::common::utf8_length(*q) > domain::kSearchMaxLength) {
      throw sg::common::bad_request("Query parameter 'q' must be at most " + std::to_string(domain::kSearchMaxLength) +
                                    " characters of UTF-8 text");
    }
    query.q = std::move(q);
  }
  query.category = text_param(req, "category");
  query.branch = text_param(req, "branch");
  if (const auto sort = text_param(req, "sort")) {
    const auto parsed = domain::parse_user_sort(*sort);
    if (!parsed) {
      throw sg::common::bad_request("Query parameter 'sort' must be one of reputation, name, id");
    }
    query.sort = *parsed;
  }
  if (text_param(req, "min_reputation")) {
    query.min_reputation = sg::common::query_int(req, "min_reputation", 0, 0, std::numeric_limits<std::int32_t>::max());
  }
  query.limit = sg::common::query_int(req, "limit", domain::kDefaultPageSize, 1, domain::kMaxPageSize);
  query.offset = sg::common::query_int(req, "offset", 0, 0, 1'000'000'000);
  return query;
}

void register_routes(sg::common::HttpServer& server, app::GraphService& graph,
                     const sg::common::TokenVerifier& verifier, ReadCache& cache) {
  // GET handlers run on the I/O thread and serve cache hits there; each cache miss (and anything
  // else that reads PostgreSQL) first calls require_worker(), which moves the request to the worker
  // pool. Writes always run on the pool (see main.cpp).
  server.get("/api/v1/meta/taxonomy", [&graph](const Request&, Response& res) {
    sg::common::require_worker();
    sg::common::write_json(res, 200, domain::to_json(*graph.taxonomy()));
  });

  server.get("/api/v1/users", [&graph, &verifier, &cache](const Request& req, Response& res) {
    (void)sg::common::require_auth(req, verifier);
    const auto query = parse_user_query(req);
    write_cached(res, cache.search(search_key(query), [&] {
      sg::common::require_worker();
      return render(domain::to_json(graph.list_users(query)));
    }));
  });

  server.get("/api/v1/users/:id", [&graph, &verifier, &cache](const Request& req, Response& res) {
    (void)sg::common::require_auth(req, verifier);
    const auto id = sg::common::path_id(req, "id");
    write_cached(res, cache.member(ReadCache::Kind::profile, id, [&] {
      sg::common::require_worker();
      return render(domain::to_json(graph.profile(id)));
    }));
  });

  server.get("/api/v1/me/profile", [&graph, &verifier, &cache](const Request& req, Response& res) {
    const auto caller = sg::common::require_auth(req, verifier).user_id;
    write_cached(res, cache.member(ReadCache::Kind::profile, caller,
                                   [&] {
      sg::common::require_worker();
      return render(domain::to_json(graph.my_profile(caller)));
    }));
  });

  server.get("/api/v1/me/connections", [&graph, &verifier, &cache](const Request& req, Response& res) {
    const auto caller = sg::common::require_auth(req, verifier).user_id;
    write_cached(res, cache.member(ReadCache::Kind::connections, caller, [&] {
      sg::common::require_worker();
      const auto list = graph.my_connections(caller);
      json items = json::array();
      for (const auto& item : list.items) {
        items.push_back(domain::to_json(item));
      }
      return render({{"items", std::move(items)}, {"graph_version", list.graph_version}});
    }));
  });

  // Writes invalidate both members on this replica as soon as they commit (read-your-writes);
  // other replicas follow from graph.events.
  server.post("/api/v1/me/connections", [&graph, &verifier, &cache](const Request& req, Response& res) {
    const auto claims = sg::common::require_auth(req, verifier);
    const auto other = target_user_id(sg::common::parse_json_body(req));
    const auto added = graph.connect(claims.user_id, other);
    cache.invalidate(claims.user_id);
    cache.invalidate(other);
    json body = domain::to_json(added.connection);
    body["graph_version"] = added.graph_version;
    sg::common::write_json(res, 201, body);
  });

  server.del("/api/v1/me/connections/:user_id", [&graph, &verifier, &cache](const Request& req, Response& res) {
    const auto claims = sg::common::require_auth(req, verifier);
    const auto other = sg::common::path_id(req, "user_id");
    const auto version = graph.disconnect(claims.user_id, other);
    cache.invalidate(claims.user_id);
    cache.invalidate(other);
    sg::common::write_json(res, 200, {{"graph_version", version}});
  });

  server.get("/internal/v1/snapshot", [&graph](const Request&, Response& res) {
    sg::common::require_worker();
    sg::common::write_json(res, 200, domain::to_json(graph.snapshot()));
  });
}

}  // namespace sg::graph_service::http
