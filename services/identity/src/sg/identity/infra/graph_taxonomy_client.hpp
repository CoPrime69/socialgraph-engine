#pragma once

/// @file graph_taxonomy_client.hpp
/// TaxonomyProvider backed by graph-service `GET /api/v1/meta/taxonomy` (contracts §10.3).
///
/// The taxonomy is owned by graph-service; identity keeps a cached copy to validate the category
/// and branch of new registrations. A background thread loads it at startup, retries every
/// `retry_interval` until graph-service answers with a usable taxonomy, then refreshes it every
/// `refresh_interval`. A failed refresh keeps the last good copy.

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "sg/identity/app/taxonomy_provider.hpp"
#include "sg/identity/domain/taxonomy.hpp"

namespace sg::identity::infra {

/// Parses `{ "categories": [names], "branches": [names], ... }`.
/// @throws std::invalid_argument when either list is missing or holds anything but non-empty strings.
[[nodiscard]] domain::Taxonomy parse_taxonomy(const nlohmann::json& body);

class GraphTaxonomyClient final : public app::TaxonomyProvider {
 public:
  struct Options {
    std::string base_url;  ///< e.g. "http://graph:8082"
    std::chrono::milliseconds refresh_interval{std::chrono::minutes(5)};
    std::chrono::milliseconds retry_interval{std::chrono::seconds(2)};
    std::chrono::seconds connect_timeout{2};
    std::chrono::seconds read_timeout{5};
  };

  explicit GraphTaxonomyClient(Options options);
  ~GraphTaxonomyClient() override;
  GraphTaxonomyClient(const GraphTaxonomyClient&) = delete;
  GraphTaxonomyClient& operator=(const GraphTaxonomyClient&) = delete;

  /// Starts the background loader (first attempt immediately).
  void start();
  /// Stops and joins the background loader. Idempotent.
  void stop();

  /// Fetches once from the calling thread; true when a usable taxonomy was stored.
  bool refresh_once();

  [[nodiscard]] std::shared_ptr<const domain::Taxonomy> current() const override;

 private:
  void run();

  Options options_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_ = false;
  std::shared_ptr<const domain::Taxonomy> taxonomy_;
  std::chrono::steady_clock::time_point last_failure_log_{};
  std::thread thread_;
};

}  // namespace sg::identity::infra
