#pragma once

/// @file routes.hpp
/// HTTP routes of graph-service (contracts §5, §10.3, §10.5). Routes only translate between HTTP and
/// the application layer.

#include <sg/common/auth.hpp>
#include <sg/common/http.hpp>

#include "sg/graph_service/app/graph_service.hpp"
#include "sg/graph_service/domain/user_query.hpp"

namespace sg::graph_service::http {

/// Parses the query string of GET /api/v1/users.
/// @throws sg::common::ApiError 400 bad_request for a malformed parameter.
[[nodiscard]] domain::UserQuery parse_user_query(const httplib::Request& req);

/// Registers
/// * `GET    /api/v1/meta/taxonomy`             (public)
/// * `GET    /api/v1/users`                     (auth) search, filters, sort, pagination
/// * `GET    /api/v1/users/:id`                 (auth)
/// * `GET    /api/v1/me/profile`                (auth)
/// * `GET    /api/v1/me/connections`            (auth)
/// * `POST   /api/v1/me/connections`            (auth) `{ "user_id": N }` → 201
/// * `DELETE /api/v1/me/connections/:user_id`   (auth)
/// * `GET    /internal/v1/snapshot`             (internal network only; never routed by the gateway)
/// The references must outlive the server.
void register_routes(sg::common::HttpServer& server, app::GraphService& graph, const sg::common::TokenVerifier& verifier);

}  // namespace sg::graph_service::http
