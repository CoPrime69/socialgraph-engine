#pragma once

#include "analytics_engine.hpp"
#include "read_model.hpp"

#include <sg/common/auth.hpp>
#include <sg/common/http.hpp>

#include <chrono>

namespace sg::analytics {

struct ApiOptions {
  std::chrono::milliseconds max_consistency_wait{2000};  // upper bound for ?min_version waits
};

// Registers every public analytics route (contracts §6) on the server. The referenced objects must
// outlive the server.
void register_routes(sg::common::HttpServer& server, const ReadModel& model, AnalyticsEngine& engine,
                     const sg::common::TokenVerifier& verifier, ApiOptions options = {});

// True for requests that may wait for a newer snapshot (?min_version, up to max_consistency_wait).
// Every other request only reads the current snapshot and never blocks, so it can be served on an
// HTTP event loop; these ones belong on the worker pool (HttpServerOptions::offload).
bool may_wait(const sg::common::Request& req);

// Graphviz rendering of the whole graph, nodes coloured by community (replaces the legacy PNG export).
std::string to_dot(const Analytics& analytics);

}  // namespace sg::analytics
