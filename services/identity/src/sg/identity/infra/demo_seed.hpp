#pragma once

/// @file demo_seed.hpp
/// Seeds the demo member account (contracts §10.2). Imported Stack Overflow users are profiles in
/// graph-service, not accounts, so this is the only account identity seeds.

#include <string>

#include <sg/common/crypto.hpp>
#include <sg/common/pg.hpp>

namespace sg::identity::infra {

/// Creates the account `demo` (id 100000000, "Demo Member") with an Argon2id hash of `password`
/// when the accounts table is empty, then moves the id sequence past it. Emits no event:
/// graph-service seeds the demo profile itself. Safe to run concurrently from several instances.
/// Returns true when the account was created.
/// @throws std::invalid_argument when `password` violates the password rule (8-128 bytes).
bool seed_demo_account_if_empty(sg::common::PgPool& pool, const std::string& password,
                                const sg::common::PasswordHashParams& params);

}  // namespace sg::identity::infra
