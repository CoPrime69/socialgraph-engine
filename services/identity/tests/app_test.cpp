#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <sg/common/auth.hpp>
#include <sg/common/crypto.hpp>
#include <sg/common/errors.hpp>

#include "fakes.hpp"
#include "sg/identity/app/account_service.hpp"

namespace {

using sg::common::ApiError;
using sg::common::PasswordHashParams;
using sg::identity::app::AccountService;
using sg::identity::domain::RegistrationInput;
using sg::identity::testing::InMemoryAccountRepository;
using sg::identity::testing::SwitchableTaxonomyProvider;

RegistrationInput ada() {
  RegistrationInput in;
  in.handle = "Ada_L";
  in.name = "Ada Lovelace";
  in.password = "correct horse";
  in.category = "Web Development";
  in.branch = "javascript";
  in.location = "London";
  return in;
}

ApiError error_of(const std::function<void()>& f) {
  try {
    f();
  } catch (const ApiError& e) {
    return e;
  }
  ADD_FAILURE() << "no ApiError thrown";
  return sg::common::internal_error();
}

class AccountServiceTest : public ::testing::Test {
 protected:
  sg::common::KeyPair keys = sg::common::generate_keypair();
  sg::common::TokenIssuer issuer{keys.seed_b64, std::chrono::hours(8)};
  sg::common::TokenVerifier verifier{keys.public_key_b64};
  InMemoryAccountRepository repo;
  SwitchableTaxonomyProvider taxonomy{sg::identity::testing::shared_test_taxonomy()};
  PasswordHashParams params = PasswordHashParams::fast_for_tests();
  AccountService service{repo, taxonomy, issuer, params};
};

TEST_F(AccountServiceTest, RegisterStoresAccountEventAndIssuesToken) {
  const auto result = service.register_account(ada());
  EXPECT_EQ(result.account.id, 100000001);
  EXPECT_EQ(result.account.handle, "ada_l");
  EXPECT_EQ(result.account.name, "Ada Lovelace");
  EXPECT_TRUE(result.account.password_hash.starts_with("$argon2id$"));
  EXPECT_TRUE(sg::common::verify_password(result.account.password_hash, "correct horse"));

  const auto claims = verifier.verify(result.token);
  EXPECT_EQ(claims.user_id, 100000001);
  EXPECT_EQ(claims.handle, "ada_l");
  EXPECT_EQ(claims.name, "Ada Lovelace");
  EXPECT_EQ(claims.expires_at, result.expires_at);

  const auto events = repo.events();
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events[0].type, "user.registered");
  EXPECT_EQ(events[0].payload["user_id"], 100000001);
  EXPECT_EQ(events[0].payload["category"], "Web Development");
  EXPECT_EQ(events[0].payload["branch"], "javascript");
  EXPECT_EQ(events[0].payload["location"], "London");
  EXPECT_TRUE(events[0].payload["about"].is_null());
  EXPECT_FALSE(events[0].payload.contains("influence"));
  EXPECT_FALSE(events[0].payload.contains("password"));
}

TEST_F(AccountServiceTest, DuplicateHandleIsAConflictRegardlessOfCase) {
  (void)service.register_account(ada());
  RegistrationInput again = ada();
  again.handle = "ADA_L";
  const ApiError e = error_of([&] { (void)service.register_account(again); });
  EXPECT_EQ(e.status(), 409);
  EXPECT_EQ(e.code(), "conflict");
  EXPECT_EQ(repo.size(), 1U);
  EXPECT_EQ(repo.events().size(), 1U);
}

TEST_F(AccountServiceTest, InvalidRegistrationStoresNothing) {
  RegistrationInput bad = ada();
  bad.password = "short";
  bad.branch = "cobol";
  const ApiError e = error_of([&] { (void)service.register_account(bad); });
  EXPECT_EQ(e.status(), 422);
  ASSERT_EQ(e.details().size(), 2U);
  EXPECT_EQ(e.details()[0].field, "password");
  EXPECT_EQ(e.details()[1].field, "branch");
  EXPECT_EQ(repo.size(), 0U);
  EXPECT_TRUE(repo.events().empty());
}

TEST_F(AccountServiceTest, RegisterNeedsTheTaxonomy) {
  taxonomy.set(nullptr);  // graph-service not reached yet
  ApiError e = error_of([&] { (void)service.register_account(ada()); });
  EXPECT_EQ(e.status(), 503);
  EXPECT_EQ(e.code(), "unavailable");

  taxonomy.set(std::make_shared<const sg::identity::domain::Taxonomy>());  // graph-service not seeded
  e = error_of([&] { (void)service.register_account(ada()); });
  EXPECT_EQ(e.status(), 503);

  // Shape errors are still reported as 422 while the taxonomy is unknown.
  RegistrationInput bad = ada();
  bad.handle = "x";
  e = error_of([&] { (void)service.register_account(bad); });
  EXPECT_EQ(e.status(), 422);
  EXPECT_EQ(repo.size(), 0U);

  taxonomy.set(sg::identity::testing::shared_test_taxonomy());
  EXPECT_NO_THROW((void)service.register_account(ada()));
}

TEST_F(AccountServiceTest, LoginIsCaseInsensitiveOnTheHandle) {
  (void)service.register_account(ada());
  const auto result = service.login("ADA_l", "correct horse");
  EXPECT_EQ(result.account.handle, "ada_l");
  EXPECT_EQ(verifier.verify(result.token).user_id, 100000001);
  EXPECT_EQ(repo.logins(), 1);
}

TEST_F(AccountServiceTest, UnknownHandleAndWrongPasswordFailIdentically) {
  (void)service.register_account(ada());
  const ApiError wrong = error_of([&] { (void)service.login("ada_l", "wrong horse"); });
  const ApiError unknown = error_of([&] { (void)service.login("nobody", "correct horse"); });
  for (const ApiError* e : {&wrong, &unknown}) {
    EXPECT_EQ(e->status(), 401);
    EXPECT_EQ(e->code(), "invalid_credentials");
  }
  EXPECT_EQ(wrong.to_json(), unknown.to_json());
  EXPECT_EQ(repo.logins(), 0);
}

TEST_F(AccountServiceTest, MalformedCredentialsAreNotLookedUp) {
  (void)service.register_account(ada());
  for (const auto& [handle, password] : std::vector<std::pair<std::string, std::string>>{
           {"a d a", "correct horse"}, {"", "correct horse"}, {"ada_l", std::string(129, 'x')}}) {
    const ApiError e = error_of([&] { (void)service.login(handle, password); });
    EXPECT_EQ(e.code(), "invalid_credentials");
  }
  EXPECT_EQ(repo.lookups(), 0);
}

TEST_F(AccountServiceTest, OutdatedPasswordHashesAreUpgradedOnLogin) {
  (void)service.register_account(ada());
  PasswordHashParams stronger = params;
  stronger.opslimit += 1;
  AccountService upgraded(repo, taxonomy, issuer, stronger);
  const auto result = upgraded.login("ada_l", "correct horse");
  EXPECT_EQ(repo.rehashes(), 1);
  EXPECT_FALSE(sg::common::password_needs_rehash(result.account.password_hash, stronger));
  EXPECT_TRUE(sg::common::verify_password(repo.find_by_id(result.account.id)->password_hash, "correct horse"));
  (void)upgraded.login("ada_l", "correct horse");
  EXPECT_EQ(repo.rehashes(), 1);  // only once
}

TEST_F(AccountServiceTest, MeReturnsTheAccountOr401) {
  const auto registered = service.register_account(ada());
  EXPECT_EQ(service.me(registered.account.id).handle, "ada_l");
  repo.remove(registered.account.id);
  const ApiError e = error_of([&] { (void)service.me(registered.account.id); });
  EXPECT_EQ(e.status(), 401);
  EXPECT_EQ(e.code(), "unauthenticated");
}

}  // namespace
