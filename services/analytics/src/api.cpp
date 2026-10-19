#include "api.hpp"

#include <sg/common/errors.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <sstream>
#include <string>

namespace sg::analytics {

namespace {

using nlohmann::json;
using sg::common::query_int;
using sg::common::query_param;
namespace graph = sg::graph;

json user_ref(const UserInfo& user) {
  return {{"id", user.id},
          {"handle", user.handle},
          {"name", user.name},
          {"category", user.category},
          {"branch", user.branch},
          {"reputation", user.reputation}};
}

// Every route: authenticate, honour ?min_version (read-your-writes), then hand the current analytics.
template <class Body>
sg::common::Handler authenticated(const ReadModel& model, AnalyticsEngine& engine,
                                  const sg::common::TokenVerifier& verifier, ApiOptions options, Body body) {
  return [&model, &engine, &verifier, options, body](const httplib::Request& req, httplib::Response& res) {
    const auto claims = sg::common::require_auth(req, verifier);
    if (auto min_version = query_param(req, "min_version")) {
      const auto target = query_int(req, "min_version", 0, 0, std::numeric_limits<long long>::max());
      if (!model.wait_for_version(static_cast<std::uint64_t>(target), options.max_consistency_wait)) {
        res.set_header("X-Graph-Stale", "true");
      }
    }
    const auto analytics = engine.current();
    res.set_header("X-Graph-Version", std::to_string(analytics->version()));
    body(req, res, claims, *analytics);
  };
}

graph::NodeId require_node(const Analytics& analytics, std::int64_t user_id, std::string_view what) {
  auto node = analytics.node(user_id);
  if (!node) throw sg::common::not_found(std::string(what) + " not found");
  return *node;
}

template <class T>
std::string most_common(const std::map<T, std::size_t>& counts) {
  // std::map iteration is ordered, so ties resolve to the alphabetically first value.
  auto best = std::max_element(counts.begin(), counts.end(),
                               [](const auto& l, const auto& r) { return l.second < r.second; });
  return best == counts.end() ? std::string{} : best->first;
}

}  // namespace

std::string to_dot(const Analytics& analytics) {
  static constexpr std::array<const char*, 12> kPalette{"#4e79a7", "#f28e2b", "#e15759", "#76b7b2",
                                                        "#59a14f", "#edc948", "#b07aa1", "#ff9da7",
                                                        "#9c755f", "#bab0ac", "#86bcb6", "#d37295"};
  auto escape = [](const std::string& value) {
    std::string out;
    for (char c : value) {
      if (c == '"' || c == '\\') out.push_back('\\');
      out.push_back(c);
    }
    return out;
  };
  std::ostringstream dot;
  dot << "graph SocialNetwork {\n"
      << "  graph [layout=neato, overlap=false, splines=true];\n"
      << "  node [shape=circle, style=filled, fontname=\"Helvetica\", fontsize=10];\n";
  const auto& users = analytics.state->users;
  for (std::size_t i = 0; i < users.size(); ++i) {
    const auto community = analytics.communities.membership[i];
    dot << "  u" << users[i].id << " [label=\"" << escape(users[i].name) << "\", fillcolor=\""
        << kPalette[community % kPalette.size()] << "\", community=" << community << "];\n";
  }
  for (const auto& edge : analytics.state->edges) {
    dot << "  u" << edge.user_a << " -- u" << edge.user_b << " [penwidth=" << 0.5 + 2.0 * edge.strength
        << "];\n";
  }
  dot << "}\n";
  return dot.str();
}

void register_routes(sg::common::HttpServer& server, const ReadModel& model, AnalyticsEngine& engine,
                     const sg::common::TokenVerifier& verifier, ApiOptions options) {
  auto route = [&](auto body) { return authenticated(model, engine, verifier, options, body); };

  server.get("/api/v1/analytics/summary", route([](const auto&, auto& res, const auto&, const Analytics& a) {
    const double n = static_cast<double>(a.graph.node_count());
    const double m = static_cast<double>(a.graph.edge_count());
    sg::common::write_json(res, 200,
                           {{"graph_version", a.version()},
                            {"users", a.graph.node_count()},
                            {"connections", a.graph.edge_count()},
                            {"density", n > 1 ? 2.0 * m / (n * (n - 1.0)) : 0.0},
                            {"average_degree", n > 0 ? 2.0 * m / n : 0.0},
                            {"components", a.components.count},
                            {"largest_component", a.components.largest},
                            {"communities", a.communities.community_count},
                            {"modularity", a.communities.modularity},
                            {"betweenness_approximate", a.betweenness_approximate},
                            {"computed_at", a.computed_at},
                            {"compute_ms", a.compute_ms}});
  }));

  server.get("/api/v1/analytics/influencers", route([](const auto& req, auto& res, const auto&, const Analytics& a) {
    const auto metric_param = query_param(req, "metric").value_or("pagerank");
    const auto metric = parse_metric(metric_param);
    if (!metric) throw sg::common::bad_request("metric must be one of pagerank, betweenness, closeness, degree");
    const auto limit = static_cast<std::size_t>(query_int(req, "limit", 10, 1, 100));
    const auto& order = a.order[static_cast<std::size_t>(*metric)];
    const auto& score = a.metric(*metric);
    json items = json::array();
    for (std::size_t r = 0; r < std::min(limit, order.size()); ++r) {
      items.push_back({{"rank", r + 1}, {"user", user_ref(a.user(order[r]))}, {"score", score[order[r]]}});
    }
    sg::common::write_json(res, 200,
                           {{"graph_version", a.version()}, {"metric", metric_name(*metric)}, {"items", items}});
  }));

  server.get("/api/v1/analytics/communities", route([](const auto& req, auto& res, const auto&, const Analytics& a) {
    const auto member_limit = static_cast<std::size_t>(query_int(req, "member_limit", 100, 0, 10000));
    const std::size_t k = a.communities.community_count;
    std::vector<std::vector<graph::NodeId>> members(k);
    for (graph::NodeId v = 0; v < a.communities.membership.size(); ++v) {
      members[a.communities.membership[v]].push_back(v);
    }
    const auto& pagerank = a.metric(Metric::pagerank);
    json items = json::array();
    for (std::size_t c = 0; c < k; ++c) {
      std::map<std::string, std::size_t> categories;
      std::map<std::string, std::size_t> branches;
      for (auto v : members[c]) {
        ++categories[a.user(v).category];
        ++branches[a.user(v).branch];
      }
      // Most influential members first, so a truncated list still shows the important people.
      std::ranges::stable_sort(members[c], [&](graph::NodeId l, graph::NodeId r) { return pagerank[l] > pagerank[r]; });
      json listed = json::array();
      for (std::size_t i = 0; i < std::min(member_limit, members[c].size()); ++i) {
        listed.push_back(user_ref(a.user(members[c][i])));
      }
      items.push_back({{"id", c},
                       {"size", members[c].size()},
                       {"dominant_category", most_common(categories)},
                       {"dominant_branch", most_common(branches)},
                       {"members", listed}});
    }
    sg::common::write_json(res, 200,
                           {{"graph_version", a.version()}, {"modularity", a.communities.modularity}, {"items", items}});
  }));

  // The full graph (~18k nodes, ~200k edges on the real dataset) is too large for a browser to lay
  // out, so the view is bounded: either the top `limit` users by PageRank, or the ego network of
  // `focus` (its neighbours up to `depth` hops, capped by `limit`). Edges are those induced by the
  // selected nodes; `total_nodes` / `total_edges` report the full size.
  server.get("/api/v1/analytics/graph", route([](const auto& req, auto& res, const auto&, const Analytics& a) {
    const auto limit = static_cast<std::size_t>(query_int(req, "limit", 1500, 1, 100000));
    const auto n = a.graph.node_count();
    const auto& pagerank = a.metric(Metric::pagerank);
    std::vector<char> selected(n, 0);
    std::vector<graph::NodeId> nodes_out;
    if (query_param(req, "focus")) {
      const auto focus = require_node(a, query_int(req, "focus", 0, 1, std::numeric_limits<long long>::max()), "User");
      const auto depth = query_int(req, "depth", 1, 1, 2);
      std::vector<graph::NodeId> frontier{focus};
      selected[focus] = 1;
      nodes_out.push_back(focus);
      for (long long d = 0; d < depth && nodes_out.size() < limit; ++d) {
        std::vector<graph::NodeId> next;
        for (auto u : frontier) {
          for (auto v : a.graph.neighbors(u)) {
            if (!selected[v]) { selected[v] = 1; next.push_back(v); }
          }
        }
        // Keep the most influential neighbours when the ring is larger than the remaining budget.
        std::ranges::stable_sort(next, [&](graph::NodeId l, graph::NodeId r) { return pagerank[l] > pagerank[r]; });
        if (next.size() > limit - nodes_out.size()) {
          for (std::size_t i = limit - nodes_out.size(); i < next.size(); ++i) selected[next[i]] = 0;
          next.resize(limit - nodes_out.size());
        }
        nodes_out.insert(nodes_out.end(), next.begin(), next.end());
        frontier = std::move(next);
      }
    } else {
      const auto& order = a.order[static_cast<std::size_t>(Metric::pagerank)];
      for (std::size_t i = 0; i < std::min(limit, order.size()); ++i) {
        selected[order[i]] = 1;
        nodes_out.push_back(order[i]);
      }
    }
    json nodes = json::array();
    for (auto v : nodes_out) {
      const auto& u = a.user(v);
      nodes.push_back({{"id", u.id},
                       {"handle", u.handle},
                       {"name", u.name},
                       {"category", u.category},
                       {"branch", u.branch},
                       {"reputation", u.reputation},
                       {"community", a.communities.membership[v]},
                       {"pagerank", pagerank[v]},
                       {"degree", a.graph.degree(v)}});
    }
    json edges = json::array();
    for (const auto& e : a.state->edges) {
      if (selected[*a.node(e.user_a)] && selected[*a.node(e.user_b)]) {
        edges.push_back({{"source", e.user_a}, {"target", e.user_b}, {"strength", e.strength}});
      }
    }
    sg::common::write_json(res, 200,
                           {{"graph_version", a.version()},
                            {"total_nodes", n},
                            {"total_edges", a.graph.edge_count()},
                            {"nodes", nodes},
                            {"edges", edges}});
  }));

  server.get("/api/v1/analytics/graph.dot", route([](const auto&, auto& res, const auto&, const Analytics& a) {
    res.set_content(to_dot(a), "text/vnd.graphviz; charset=utf-8");
  }));

  server.get("/api/v1/me/recommendations", route([](const auto& req, auto& res, const auto& claims, const Analytics& a) {
    const auto me = require_node(a, claims.user_id, "Profile");
    graph::RecommendOptions recommend_options;
    recommend_options.limit = static_cast<std::size_t>(query_int(req, "limit", 10, 1, 50));
    const auto recommendations =
        graph::recommend(a.graph, a.profiles, a.communities.membership, me, recommend_options);
    json items = json::array();
    for (const auto& r : recommendations) {
      items.push_back({{"user", user_ref(a.user(r.node))},
                       {"score", r.score},
                       {"mutual_connections", r.mutual},
                       {"reasons",
                        {{"adamic_adar", r.adamic_adar},
                         {"affinity", r.affinity},
                         {"same_category", r.same_category},
                         {"same_branch", r.same_branch},
                         {"same_community", r.same_community}}}});
    }
    sg::common::write_json(res, 200, {{"graph_version", a.version()}, {"items", items}});
  }));

  server.get("/api/v1/paths", route([](const auto& req, auto& res, const auto& claims, const Analytics& a) {
    if (!query_param(req, "to")) throw sg::common::bad_request("Query parameter 'to' is required");
    const auto to_id = query_int(req, "to", 0, 1, std::numeric_limits<long long>::max());
    const auto from_id = query_int(req, "from", claims.user_id, 1, std::numeric_limits<long long>::max());
    const auto from = require_node(a, from_id, "Source user");
    const auto to = require_node(a, to_id, "Target user");
    const auto path = graph::shortest_path(a.graph, from, to);
    json body = {{"graph_version", a.version()}, {"found", path.has_value()}};
    json users = json::array();
    if (path) {
      for (auto v : *path) users.push_back(user_ref(a.user(v)));
      body["degrees"] = path->size() - 1;
    } else {
      body["degrees"] = nullptr;
    }
    body["path"] = users;
    sg::common::write_json(res, 200, body);
  }));

  server.get("/api/v1/users/:id/metrics", route([](const auto& req, auto& res, const auto&, const Analytics& a) {
    const auto v = require_node(a, sg::common::path_id(req, "id"), "User");
    auto at = [&](Metric m) { return a.metric(m)[v]; };
    auto rank = [&](Metric m) { return a.ranks[static_cast<std::size_t>(m)][v]; };
    sg::common::write_json(res, 200,
                           {{"graph_version", a.version()},
                            {"user", user_ref(a.user(v))},
                            {"degree", a.graph.degree(v)},
                            {"pagerank", at(Metric::pagerank)},
                            {"betweenness", at(Metric::betweenness)},
                            {"closeness", at(Metric::closeness)},
                            {"community", a.communities.membership[v]},
                            {"ranks",
                             {{"pagerank", rank(Metric::pagerank)},
                              {"betweenness", rank(Metric::betweenness)},
                              {"closeness", rank(Metric::closeness)},
                              {"degree", rank(Metric::degree)}}}});
  }));
}

}  // namespace sg::analytics
