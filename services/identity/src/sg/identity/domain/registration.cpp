#include "sg/identity/domain/registration.hpp"

#include <sg/common/errors.hpp>
#include <sg/common/validation.hpp>

namespace sg::identity::domain {
namespace {

using sg::common::Validator;

/// Optional free text: absent or blank -> nullopt; otherwise validated.
std::optional<std::string> optional_text(Validator& v, const std::optional<std::string>& raw, const char* field,
                                         std::size_t max, bool allow_newlines, const std::string& rule) {
  if (!raw) {
    return std::nullopt;
  }
  const bool blank = raw->find_first_not_of(" \t\r\n") == std::string::npos;
  if (blank) {
    return std::nullopt;
  }
  auto text = sg::common::normalize_text(*raw, max, allow_newlines);
  v.require(text.has_value(), field, rule);
  return text;
}

}  // namespace

Registration validate_registration(const RegistrationInput& input, const Taxonomy* taxonomy) {
  Validator v;
  Registration out;

  if (!input.handle) {
    v.add("handle", "is required");
  } else {
    out.handle = sg::common::normalize_handle(*input.handle);
    v.require(sg::common::is_valid_handle(out.handle), "handle", std::string(sg::common::kHandleRule));
  }

  if (!input.name) {
    v.add("name", "is required");
  } else if (auto name = sg::common::normalize_name(*input.name)) {
    out.name = std::move(*name);
  } else {
    v.add("name", std::string(sg::common::kNameRule));
  }

  if (!input.password) {
    v.add("password", "is required");
  } else {
    v.require(sg::common::is_valid_password(*input.password), "password", std::string(sg::common::kPasswordRule));
    out.password = *input.password;
  }

  const auto taxonomy_field = [&](const std::optional<std::string>& raw, const char* field, std::string& target,
                                  bool (Taxonomy::*known)(std::string_view) const) {
    if (!raw) {
      v.add(field, "is required");
      return;
    }
    auto value = sg::common::normalize_text(*raw, kTaxonomyNameMaxLength);
    if (!value) {
      v.add(field, "must be 1-64 characters");
      return;
    }
    if (taxonomy != nullptr && !(taxonomy->*known)(*value)) {
      v.add(field, std::string("must be one of the ") + (std::string_view(field) == "category" ? "categories" : "branches") +
                       " of GET /api/v1/meta/taxonomy");
      return;
    }
    target = std::move(*value);
  };
  taxonomy_field(input.category, "category", out.category, &Taxonomy::has_category);
  taxonomy_field(input.branch, "branch", out.branch, &Taxonomy::has_branch);

  out.location = optional_text(v, input.location, "location", kLocationMaxLength, false,
                               "must be at most 100 characters without control characters");
  out.about = optional_text(v, input.about, "about", kAboutMaxLength, true,
                            "must be at most 500 characters without control characters");

  v.throw_if_failed();
  return out;
}

nlohmann::json user_registered_payload(const Account& account, const Registration& registration) {
  return {
      {"user_id", account.id},
      {"handle", account.handle},
      {"name", account.name},
      {"category", registration.category},
      {"branch", registration.branch},
      {"location", registration.location ? nlohmann::json(*registration.location) : nlohmann::json(nullptr)},
      {"about", registration.about ? nlohmann::json(*registration.about) : nlohmann::json(nullptr)},
  };
}

}  // namespace sg::identity::domain
