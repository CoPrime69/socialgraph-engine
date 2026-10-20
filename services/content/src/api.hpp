#pragma once

#include "model.hpp"

#include <sg/common/auth.hpp>
#include <sg/common/http.hpp>

namespace sg::content {

struct ApiOptions {
  // Rendered responses kept in memory. The content is an imported, read-only dataset, so entries
  // never need invalidating; the LRU bound only caps memory (0 disables the cache).
  std::size_t cache_entries = 50'000;
};

// Registers the content routes (contracts §10.6). Request validation lives here; the repository
// receives only well-formed, bounded queries. The objects must outlive the server.
void register_routes(sg::common::HttpServer& server, ContentRepository& repository,
                     const sg::common::TokenVerifier& verifier, ApiOptions options = {});

nlohmann::json to_json(const Activity& activity);
nlohmann::json to_json(const QuestionSummary& question);
nlohmann::json to_json(const QuestionDetail& detail);

}  // namespace sg::content
