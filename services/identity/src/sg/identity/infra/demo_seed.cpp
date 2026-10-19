#include "sg/identity/infra/demo_seed.hpp"

#include <stdexcept>

#include <sg/common/log.hpp>
#include <sg/common/validation.hpp>

#include "sg/identity/domain/account.hpp"
#include "sg/identity/infra/migrations.hpp"

namespace sg::identity::infra {

bool seed_demo_account_if_empty(sg::common::PgPool& pool, const std::string& password,
                                const sg::common::PasswordHashParams& params) {
  if (!sg::common::is_valid_password(password)) {
    throw std::invalid_argument("SEED_DEMO_PASSWORD must be 8-128 bytes");
  }
  const bool empty = pool.with_transaction(
      [](pqxx::work& tx) { return !tx.query_value<bool>("SELECT EXISTS (SELECT 1 FROM accounts)"); });
  if (!empty) {
    return false;
  }
  const std::string hash = sg::common::hash_password(password, params);  // outside the transaction
  const bool created = pool.with_transaction([&](pqxx::work& tx) {
    tx.exec_params("SELECT pg_advisory_xact_lock($1)", kMigrationLockKey);
    if (tx.query_value<bool>("SELECT EXISTS (SELECT 1 FROM accounts)")) {
      return false;  // another instance seeded (or someone registered) meanwhile
    }
    tx.exec_params("INSERT INTO accounts (id, handle, name, password_hash) VALUES ($1, $2, $3, $4)",
                   domain::kDemoAccountId, std::string(domain::kDemoHandle), std::string(domain::kDemoName), hash);
    tx.exec_params("SELECT setval(pg_get_serial_sequence('accounts', 'id'), GREATEST($1, "
                   "(SELECT last_value FROM accounts_id_seq)), true)",
                   domain::kDemoAccountId);
    return true;
  });
  if (created) {
    sg::common::log_info("demo account seeded", {{"user_id", domain::kDemoAccountId}, {"handle", domain::kDemoHandle}});
  }
  return created;
}

}  // namespace sg::identity::infra
