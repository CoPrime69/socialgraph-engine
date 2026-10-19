#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <sg/common/errors.hpp>
#include <sg/common/validation.hpp>

#include "fakes.hpp"
#include "sg/identity/domain/registration.hpp"

namespace {

using sg::common::ApiError;
using sg::common::FieldError;
using sg::identity::domain::Account;
using sg::identity::domain::RegistrationInput;
using sg::identity::domain::Taxonomy;
using sg::identity::domain::validate_registration;

RegistrationInput valid_input() {
  RegistrationInput in;
  in.handle = "Ada_L";
  in.name = "  Ada Lovelace  ";
  in.password = "correct horse";
  in.category = "Web Development";
  in.branch = "javascript";
  return in;
}

/// The details of the 422 thrown for `input`, or a test failure when it is accepted.
std::vector<FieldError> errors_of(const RegistrationInput& input, const Taxonomy* taxonomy) {
  try {
    (void)validate_registration(input, taxonomy);
  } catch (const ApiError& e) {
    EXPECT_EQ(e.status(), 422);
    EXPECT_EQ(e.code(), "validation_failed");
    return e.details();
  }
  ADD_FAILURE() << "input was accepted";
  return {};
}

std::vector<FieldError> single(const char* field, std::string message) { return {{field, std::move(message)}}; }

class RegistrationRules : public ::testing::Test {
 protected:
  Taxonomy taxonomy = sg::identity::testing::test_taxonomy();
};

TEST_F(RegistrationRules, ValidInputIsNormalised) {
  RegistrationInput in = valid_input();
  in.location = "  London, UK ";
  in.about = "Line one\nLine two\twith a tab";
  const auto reg = validate_registration(in, &taxonomy);
  EXPECT_EQ(reg.handle, "ada_l");
  EXPECT_EQ(reg.name, "Ada Lovelace");
  EXPECT_EQ(reg.password, "correct horse");
  EXPECT_EQ(reg.category, "Web Development");
  EXPECT_EQ(reg.branch, "javascript");
  EXPECT_EQ(reg.location, "London, UK");
  EXPECT_EQ(reg.about, "Line one\nLine two\twith a tab");
}

TEST_F(RegistrationRules, MissingFieldsAreAllReported) {
  const auto errors = errors_of(RegistrationInput{}, &taxonomy);
  const std::vector<FieldError> expected{{"handle", "is required"},
                                         {"name", "is required"},
                                         {"password", "is required"},
                                         {"category", "is required"},
                                         {"branch", "is required"}};
  EXPECT_EQ(errors, expected);
}

TEST_F(RegistrationRules, HandleRule) {
  const std::vector<std::string> bad_handles{"ab", std::string(33, 'a'), "has space", "a-b", "dot.ted", "\xc3\xa9mile", ""};
  for (const auto& bad : bad_handles) {
    RegistrationInput in = valid_input();
    in.handle = bad;
    EXPECT_EQ(errors_of(in, &taxonomy), single("handle", std::string(sg::common::kHandleRule))) << bad;
  }
  const std::vector<std::string> good_handles{"abc", std::string(32, 'z'), "UPPER_case_9"};
  for (const auto& good : good_handles) {
    RegistrationInput in = valid_input();
    in.handle = good;
    EXPECT_NO_THROW((void)validate_registration(in, &taxonomy)) << good;
  }
}

TEST_F(RegistrationRules, NameRule) {
  std::string sixty_four_accents;
  for (int i = 0; i < 64; ++i) {
    sixty_four_accents += "\xc3\xa9";  // é: 64 code points, 128 bytes
  }
  const std::vector<std::string> bad_names{"   ", "", std::string(65, 'n'), "tab\there", "bell\x07", "bad \xff utf8",
                                           sixty_four_accents + "x"};
  for (const auto& bad : bad_names) {
    RegistrationInput in = valid_input();
    in.name = bad;
    EXPECT_EQ(errors_of(in, &taxonomy), single("name", std::string(sg::common::kNameRule))) << bad;
  }
  RegistrationInput in = valid_input();
  in.name = sixty_four_accents;
  EXPECT_EQ(validate_registration(in, &taxonomy).name, sixty_four_accents);
}

TEST_F(RegistrationRules, PasswordRuleCountsBytes) {
  const std::vector<std::string> bad_passwords{std::string(7, 'p'), std::string(129, 'p'), ""};
  for (const auto& bad : bad_passwords) {
    RegistrationInput in = valid_input();
    in.password = bad;
    EXPECT_EQ(errors_of(in, &taxonomy), single("password", std::string(sg::common::kPasswordRule))) << bad.size();
  }
  const std::vector<std::string> good_passwords{std::string(8, 'p'), std::string(128, 'p'), "        "};
  for (const auto& good : good_passwords) {
    RegistrationInput in = valid_input();
    in.password = good;
    EXPECT_EQ(validate_registration(in, &taxonomy).password, good);  // never trimmed
  }
}

TEST_F(RegistrationRules, CategoryAndBranchMustExistInTheTaxonomy) {
  RegistrationInput in = valid_input();
  in.category = "Engineering";  // the retired toy taxonomy
  in.branch = "software";
  const std::vector<FieldError> expected{
      {"category", "must be one of the categories of GET /api/v1/meta/taxonomy"},
      {"branch", "must be one of the branches of GET /api/v1/meta/taxonomy"}};
  EXPECT_EQ(errors_of(in, &taxonomy), expected);

  in.category = "web development";  // names are case-sensitive, exactly as listed
  in.branch = "c++";
  EXPECT_EQ(errors_of(in, &taxonomy), single("category", "must be one of the categories of GET /api/v1/meta/taxonomy"));

  in.category = std::string(65, 'c');
  in.branch = "   ";
  const std::vector<FieldError> too_long{{"category", "must be 1-64 characters"}, {"branch", "must be 1-64 characters"}};
  EXPECT_EQ(errors_of(in, &taxonomy), too_long);
}

TEST_F(RegistrationRules, WithoutTaxonomyOnlyTheShapeIsChecked) {
  RegistrationInput in = valid_input();
  in.category = "Anything";
  in.branch = "goes";
  EXPECT_NO_THROW((void)validate_registration(in, nullptr));
}

TEST_F(RegistrationRules, OptionalLocationAndAbout) {
  RegistrationInput in = valid_input();
  in.location = std::string(100, 'l');
  in.about = std::string(500, 'a');
  auto reg = validate_registration(in, &taxonomy);
  EXPECT_EQ(reg.location->size(), 100U);
  EXPECT_EQ(reg.about->size(), 500U);

  in.location = "   ";
  in.about = "\n\n";
  reg = validate_registration(in, &taxonomy);
  EXPECT_FALSE(reg.location.has_value());
  EXPECT_FALSE(reg.about.has_value());

  in.location = std::string(101, 'l');
  in.about = std::string(501, 'a');
  const std::vector<FieldError> expected{{"location", "must be at most 100 characters without control characters"},
                                         {"about", "must be at most 500 characters without control characters"}};
  EXPECT_EQ(errors_of(in, &taxonomy), expected);

  in.location = "two\nlines";  // line breaks are only allowed in "about"
  in.about = "nul\x01";
  EXPECT_EQ(errors_of(in, &taxonomy), expected);
}

TEST(UserRegisteredPayload, MatchesTheContract) {
  RegistrationInput in = valid_input();
  in.about = "Hi";
  const Taxonomy taxonomy = sg::identity::testing::test_taxonomy();
  const auto reg = validate_registration(in, &taxonomy);
  Account account;
  account.id = 100000001;
  account.handle = reg.handle;
  account.name = reg.name;
  const auto payload = sg::identity::domain::user_registered_payload(account, reg);
  EXPECT_EQ(payload, nlohmann::json::parse(R"({"user_id":100000001,"handle":"ada_l","name":"Ada Lovelace",
      "category":"Web Development","branch":"javascript","location":null,"about":"Hi"})"));
}

}  // namespace
