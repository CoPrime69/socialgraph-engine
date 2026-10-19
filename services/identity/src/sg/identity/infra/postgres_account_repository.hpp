#pragma once

/// @file postgres_account_repository.hpp
/// AccountRepository on PostgreSQL. All SQL is parameterized.

#include <sg/common/pg.hpp>

#include "sg/identity/app/account_repository.hpp"

namespace sg::identity::infra {

class PostgresAccountRepository final : public app::AccountRepository {
 public:
  explicit PostgresAccountRepository(sg::common::PgPool& pool) : pool_(pool) {}

  domain::Account create(const domain::Registration& registration, const std::string& password_hash) override;
  [[nodiscard]] std::optional<domain::Account> find_by_handle(const std::string& handle) override;
  [[nodiscard]] std::optional<domain::Account> find_by_id(std::int64_t id) override;
  void record_login(std::int64_t id, const std::optional<std::string>& new_password_hash) override;

 private:
  sg::common::PgPool& pool_;
};

}  // namespace sg::identity::infra
