#include "sg/identity/infra/postgres_account_repository.hpp"

#include <sg/common/errors.hpp>
#include <sg/common/outbox.hpp>

namespace sg::identity::infra {
namespace {

constexpr const char* kSelectColumns =
    "SELECT id, handle, name, password_hash, extract(epoch FROM created_at)::bigint, "
    "extract(epoch FROM last_login_at)::bigint FROM accounts ";

domain::Account to_account(const pqxx::row& row) {
  domain::Account account;
  account.id = row[0].as<std::int64_t>();
  account.handle = row[1].as<std::string>();
  account.name = row[2].as<std::string>();
  account.password_hash = row[3].as<std::string>();
  account.created_at = row[4].as<std::int64_t>();
  if (!row[5].is_null()) {
    account.last_login_at = row[5].as<std::int64_t>();
  }
  return account;
}

}  // namespace

domain::Account PostgresAccountRepository::create(const domain::Registration& registration,
                                                  const std::string& password_hash) {
  try {
    return pool_.with_transaction([&](pqxx::work& tx) {
      const auto result = tx.exec_params(
          "INSERT INTO accounts (handle, name, password_hash) VALUES ($1, $2, $3) "
          "ON CONFLICT (handle) DO NOTHING "
          "RETURNING id, handle, name, password_hash, extract(epoch FROM created_at)::bigint, NULL::bigint",
          registration.handle, registration.name, password_hash);
      if (result.empty()) {
        throw sg::common::conflict("Handle is already taken");
      }
      domain::Account account = to_account(result[0]);
      sg::common::enqueue_event(tx, "user.registered", domain::user_registered_payload(account, registration));
      return account;
    });
  } catch (const pqxx::unique_violation&) {
    throw sg::common::conflict("Handle is already taken");
  }
}

std::optional<domain::Account> PostgresAccountRepository::find_by_handle(const std::string& handle) {
  return pool_.with_transaction([&](pqxx::work& tx) -> std::optional<domain::Account> {
    const auto result = tx.exec_params(std::string(kSelectColumns) + "WHERE handle = $1", handle);
    if (result.empty()) {
      return std::nullopt;
    }
    return to_account(result[0]);
  });
}

std::optional<domain::Account> PostgresAccountRepository::find_by_id(std::int64_t id) {
  return pool_.with_transaction([&](pqxx::work& tx) -> std::optional<domain::Account> {
    const auto result = tx.exec_params(std::string(kSelectColumns) + "WHERE id = $1", id);
    if (result.empty()) {
      return std::nullopt;
    }
    return to_account(result[0]);
  });
}

void PostgresAccountRepository::record_login(std::int64_t id, const std::optional<std::string>& new_password_hash) {
  pool_.with_transaction([&](pqxx::work& tx) {
    if (new_password_hash) {
      tx.exec_params("UPDATE accounts SET last_login_at = now(), password_hash = $2 WHERE id = $1", id,
                     *new_password_hash);
    } else {
      tx.exec_params("UPDATE accounts SET last_login_at = now() WHERE id = $1", id);
    }
  });
}

}  // namespace sg::identity::infra
