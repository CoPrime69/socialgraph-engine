#pragma once

#include "model.hpp"

#include <sg/common/auth.hpp>
#include <sg/common/http.hpp>

namespace sg::content {

// Registers the content routes (contracts §10.6). Request validation lives here; the repository
// receives only well-formed, bounded queries.
void register_routes(sg::common::HttpServer& server, ContentRepository& repository,
                     const sg::common::TokenVerifier& verifier);

nlohmann::json to_json(const Activity& activity);
nlohmann::json to_json(const QuestionSummary& question);
nlohmann::json to_json(const QuestionDetail& detail);

}  // namespace sg::content
