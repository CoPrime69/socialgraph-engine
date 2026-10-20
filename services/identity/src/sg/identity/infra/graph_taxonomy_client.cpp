#include "sg/identity/infra/graph_taxonomy_client.hpp"

#include <stdexcept>
#include <utility>

#include <httplib.h>

#include <sg/common/log.hpp>

namespace sg::identity::infra {
namespace {

std::vector<std::string> string_list(const nlohmann::json& body, const char* key) {
  const auto it = body.find(key);
  if (it == body.end() || !it->is_array()) {
    throw std::invalid_argument(std::string("taxonomy: '") + key + "' must be an array");
  }
  std::vector<std::string> out;
  out.reserve(it->size());
  for (const auto& item : *it) {
    if (!item.is_string() || item.get_ref<const std::string&>().empty()) {
      throw std::invalid_argument(std::string("taxonomy: '") + key + "' must hold non-empty strings");
    }
    out.push_back(item.get<std::string>());
  }
  return out;
}

}  // namespace

domain::Taxonomy parse_taxonomy(const nlohmann::json& body) {
  if (!body.is_object()) {
    throw std::invalid_argument("taxonomy: body must be a JSON object");
  }
  domain::Taxonomy taxonomy;
  taxonomy.categories = string_list(body, "categories");
  taxonomy.branches = string_list(body, "branches");
  return taxonomy;
}

GraphTaxonomyClient::GraphTaxonomyClient(Options options) : options_(std::move(options)) {
  while (!options_.base_url.empty() && options_.base_url.back() == '/') {
    options_.base_url.pop_back();  // httplib expects scheme://host[:port] without a path
  }
  if (options_.base_url.empty()) {
    throw std::invalid_argument("GRAPH_SERVICE_URL must not be empty");
  }
}

GraphTaxonomyClient::~GraphTaxonomyClient() { stop(); }

void GraphTaxonomyClient::start() {
  std::lock_guard lock(mutex_);
  if (thread_.joinable()) {
    return;
  }
  stopping_ = false;
  thread_ = std::thread([this] { run(); });
}

void GraphTaxonomyClient::stop() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

std::shared_ptr<const domain::Taxonomy> GraphTaxonomyClient::current() const {
  std::lock_guard lock(mutex_);
  return taxonomy_;
}

bool GraphTaxonomyClient::refresh_once() {
  std::string error;
  try {
    httplib::Client client(options_.base_url);
    client.set_connection_timeout(options_.connect_timeout);
    client.set_read_timeout(options_.read_timeout);
    client.set_follow_location(false);
    const auto response = client.Get("/api/v1/meta/taxonomy", {{"Accept", "application/json"}});
    if (!response) {
      error = "request failed: " + httplib::to_string(response.error());
    } else if (response->status != 200) {
      error = "graph-service answered HTTP " + std::to_string(response->status);
    } else {
      auto taxonomy = std::make_shared<const domain::Taxonomy>(parse_taxonomy(nlohmann::json::parse(response->body)));
      if (!taxonomy->usable()) {
        error = "graph-service taxonomy is empty (dataset not loaded yet)";
      } else {
        bool changed = false;
        {
          std::lock_guard lock(mutex_);
          changed = !taxonomy_ || taxonomy_->categories != taxonomy->categories ||
                    taxonomy_->branches != taxonomy->branches;
          taxonomy_ = std::move(taxonomy);
          last_failure_log_ = {};
          if (changed) {
            sg::common::log_info("taxonomy loaded from graph-service", {{"categories", taxonomy_->categories.size()},
                                                                      {"branches", taxonomy_->branches.size()}});
          }
        }
        return true;
      }
    }
  } catch (const std::exception& e) {
    error = e.what();
  }
  // While graph-service is down this runs every retry_interval: log at most once a minute.
  const auto now = std::chrono::steady_clock::now();
  bool log_now = false;
  bool have_copy = false;
  {
    std::lock_guard lock(mutex_);
    have_copy = taxonomy_ != nullptr;
    if (last_failure_log_ == std::chrono::steady_clock::time_point{} || now - last_failure_log_ >= std::chrono::minutes(1)) {
      last_failure_log_ = now;
      log_now = true;
    }
  }
  if (log_now) {
    sg::common::log_warn("taxonomy fetch failed", {{"url", options_.base_url},
                                                    {"error", error},
                                                    {"keeping_cached_copy", have_copy}});
  }
  return false;
}

void GraphTaxonomyClient::run() {
  for (;;) {
    const bool ok = refresh_once();
    std::unique_lock lock(mutex_);
    const auto wait = ok ? options_.refresh_interval : options_.retry_interval;
    if (wake_.wait_for(lock, wait, [this] { return stopping_; })) {
      return;
    }
  }
}

}  // namespace sg::identity::infra
