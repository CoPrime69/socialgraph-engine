#pragma once

// In-memory test doubles for the identity application layer.

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <sg/common/errors.hpp>
#include <sg/common/time.hpp>

#include "sg/identity/app/account_repository.hpp"
#include "sg/identity/app/taxonomy_provider.hpp"
#include "sg/identity/domain/taxonomy.hpp"

namespace sg::identity::testing {

struct RecordedEvent {
  std::string type;
  nlohmann::json payload;
};

/// AccountRepository in memory; "outbox" events are recorded in the same critical section as the
/// account, mirroring the transactional guarantee of the PostgreSQL repository.
class InMemoryAccountRepository final : public app::AccountRepository {
 public:
  domain::Account create(const domain::Registration& registration, const std::string& password_hash) override {
    std::lock_guard lock(mutex_);
    for (const auto& [id, account] : accounts_) {
      if (account.handle == registration.handle) {
        throw sg::common::conflict("Handle is already taken");
      }
    }
    domain::Account account;
    account.id = next_id_++;
    account.handle = registration.handle;
    account.name = registration.name;
    account.password_hash = password_hash;
    account.created_at = sg::common::unix_now();
    accounts_[account.id] = account;
    events_.push_back({"user.registered", domain::user_registered_payload(account, registration)});
    return account;
  }

  std::optional<domain::Account> find_by_handle(const std::string& handle) override {
    std::lock_guard lock(mutex_);
    ++lookups_;
    for (const auto& [id, account] : accounts_) {
      if (account.handle == handle) {
        return account;
      }
    }
    return std::nullopt;
  }

  std::optional<domain::Account> find_by_id(std::int64_t id) override {
    std::lock_guard lock(mutex_);
    const auto it = accounts_.find(id);
    return it == accounts_.end() ? std::nullopt : std::optional<domain::Account>(it->second);
  }

  void record_login(std::int64_t id, const std::optional<std::string>& new_password_hash) override {
    std::lock_guard lock(mutex_);
    auto& account = accounts_.at(id);
    account.last_login_at = sg::common::unix_now();
    if (new_password_hash) {
      account.password_hash = *new_password_hash;
      ++rehashes_;
    }
    ++logins_;
  }

  // Test helpers.
  void remove(std::int64_t id) {
    std::lock_guard lock(mutex_);
    accounts_.erase(id);
  }
  [[nodiscard]] std::vector<RecordedEvent> events() const {
    std::lock_guard lock(mutex_);
    return events_;
  }
  [[nodiscard]] std::size_t size() const {
    std::lock_guard lock(mutex_);
    return accounts_.size();
  }
  [[nodiscard]] int lookups() const {
    std::lock_guard lock(mutex_);
    return lookups_;
  }
  [[nodiscard]] int logins() const {
    std::lock_guard lock(mutex_);
    return logins_;
  }
  [[nodiscard]] int rehashes() const {
    std::lock_guard lock(mutex_);
    return rehashes_;
  }

 private:
  mutable std::mutex mutex_;
  std::map<std::int64_t, domain::Account> accounts_;
  std::vector<RecordedEvent> events_;
  std::int64_t next_id_ = 100'000'001;
  int lookups_ = 0;
  int logins_ = 0;
  int rehashes_ = 0;
};

/// A TaxonomyProvider whose value tests can swap (nullptr = not loaded yet).
class SwitchableTaxonomyProvider final : public app::TaxonomyProvider {
 public:
  explicit SwitchableTaxonomyProvider(std::shared_ptr<const domain::Taxonomy> taxonomy = nullptr)
      : taxonomy_(std::move(taxonomy)) {}
  [[nodiscard]] std::shared_ptr<const domain::Taxonomy> current() const override {
    std::lock_guard lock(mutex_);
    return taxonomy_;
  }
  void set(std::shared_ptr<const domain::Taxonomy> taxonomy) {
    std::lock_guard lock(mutex_);
    taxonomy_ = std::move(taxonomy);
  }

 private:
  mutable std::mutex mutex_;
  std::shared_ptr<const domain::Taxonomy> taxonomy_;
};

/// A small slice of the real taxonomy (contracts §10.3).
inline domain::Taxonomy test_taxonomy() {
  return {{"Web Development", "Databases", "Systems Programming"}, {"javascript", "sql", "c++", "php"}};
}

inline std::shared_ptr<const domain::Taxonomy> shared_test_taxonomy() {
  return std::make_shared<const domain::Taxonomy>(test_taxonomy());
}

}  // namespace sg::identity::testing
