#include "api.hpp"

#include <sg/common/errors.hpp>

#include <algorithm>
#include <cctype>

namespace sg::content {

namespace {

using nlohmann::json;
using sg::common::query_int;
using sg::common::query_param;

constexpr std::size_t kMaxSearchLength = 100;

std::string question_url(std::int64_t id) { return "https://stackoverflow.com/q/" + std::to_string(id); }
std::string answer_url(std::int64_t id) { return "https://stackoverflow.com/a/" + std::to_string(id); }

json author_json(const std::optional<AuthorRef>& author) {
  if (!author) return nullptr;
  return {{"id", author->id}, {"display_name", author->display_name}};
}

template <class T, class F>
json page_json(const Page<T>& page, F&& item) {
  json items = json::array();
  for (const auto& value : page.items) items.push_back(item(value));
  return {{"items", items}, {"total", page.total}, {"limit", page.limit}, {"offset", page.offset}};
}

// Optional free-text query parameter: trimmed, non-empty, bounded, no control characters.
std::optional<std::string> text_param(const httplib::Request& req, const std::string& name) {
  auto value = query_param(req, name);
  if (!value) return std::nullopt;
  auto first = value->find_first_not_of(" \t");
  if (first == std::string::npos) return std::nullopt;
  auto last = value->find_last_not_of(" \t");
  std::string trimmed = value->substr(first, last - first + 1);
  if (trimmed.size() > kMaxSearchLength) {
    throw sg::common::bad_request("Query parameter '" + name + "' must be at most 100 characters");
  }
  if (std::ranges::any_of(trimmed, [](unsigned char c) { return std::iscntrl(c) != 0; })) {
    throw sg::common::bad_request("Query parameter '" + name + "' contains control characters");
  }
  return trimmed;
}

template <class Body>
sg::common::Handler authenticated(const sg::common::TokenVerifier& verifier, Body body) {
  return [&verifier, body](const httplib::Request& req, httplib::Response& res) {
    (void)sg::common::require_auth(req, verifier);
    body(req, res);
  };
}

}  // namespace

json to_json(const Activity& a) {
  json tags = json::array();
  for (const auto& t : a.top_tags) tags.push_back({{"tag", t.tag}, {"posts", t.posts}, {"score", t.score}});
  json badges = json::array();
  for (const auto& b : a.recent_badges) {
    badges.push_back({{"name", b.name}, {"class", b.badge_class}, {"awarded_at", b.awarded_at}});
  }
  json posts = json::array();
  for (const auto& p : a.top_posts) {
    posts.push_back({{"id", p.id},
                     {"type", p.is_question ? "question" : "answer"},
                     {"question_id", p.question_id},
                     {"title", p.title},
                     {"score", p.score},
                     {"view_count", p.view_count ? json(*p.view_count) : json(nullptr)},
                     {"created_at", p.created_at},
                     {"tags", p.tags},
                     {"url", p.is_question ? question_url(p.id) : answer_url(p.id)}});
  }
  return {{"user_id", a.user_id},
          {"totals",
           {{"questions", a.totals.questions},
            {"answers", a.totals.answers},
            {"accepted_answers", a.totals.accepted_answers},
            {"comments", a.totals.comments},
            {"question_score", a.totals.question_score},
            {"answer_score", a.totals.answer_score}}},
          {"top_tags", tags},
          {"badges", {{"gold", a.gold}, {"silver", a.silver}, {"bronze", a.bronze}, {"recent", badges}}},
          {"top_posts", posts}};
}

json to_json(const QuestionSummary& q) {
  return {{"id", q.id},
          {"title", q.title},
          {"tags", q.tags},
          {"score", q.score},
          {"view_count", q.view_count},
          {"answer_count", q.answer_count},
          {"created_at", q.created_at},
          {"owner", author_json(q.owner)},
          {"url", question_url(q.id)}};
}

json to_json(const QuestionDetail& d) {
  json body = to_json(d.question);
  body["accepted_answer_id"] = d.accepted_answer_id ? json(*d.accepted_answer_id) : json(nullptr);
  body["closed_at"] = d.closed_at ? json(*d.closed_at) : json(nullptr);
  body["comment_count"] = d.comment_count;
  json answers = json::array();
  for (const auto& a : d.answers) {
    answers.push_back({{"id", a.id},
                       {"owner", author_json(a.owner)},
                       {"score", a.score},
                       {"created_at", a.created_at},
                       {"is_accepted", a.is_accepted},
                       {"url", answer_url(a.id)}});
  }
  body["answers"] = answers;
  return body;
}

void register_routes(sg::common::HttpServer& server, ContentRepository& repository,
                     const sg::common::TokenVerifier& verifier) {
  server.get("/api/v1/users/:id/activity", authenticated(verifier, [&repository](const auto& req, auto& res) {
               sg::common::write_json(res, 200, to_json(repository.activity(sg::common::path_id(req, "id"))));
             }));

  server.get("/api/v1/tags", authenticated(verifier, [&repository](const auto& req, auto& res) {
               const auto prefix = text_param(req, "q");
               const auto limit = query_int(req, "limit", 20, 1, 100);
               const auto offset = query_int(req, "offset", 0, 0, 1'000'000);
               const auto page = repository.tags(prefix, limit, offset);
               sg::common::write_json(res, 200, page_json(page, [](const TagInfo& t) {
                                        return json{{"name", t.name},
                                                    {"question_count", t.question_count},
                                                    {"category", t.category ? json(*t.category) : json(nullptr)}};
                                      }));
             }));

  server.get("/api/v1/questions", authenticated(verifier, [&repository](const auto& req, auto& res) {
               QuestionQuery query;
               query.tag = text_param(req, "tag");
               query.text = text_param(req, "q");
               const auto sort = query_param(req, "sort").value_or("score");
               if (sort == "recent") {
                 query.sort = QuestionSort::recent;
               } else if (sort != "score") {
                 throw sg::common::bad_request("sort must be 'score' or 'recent'");
               }
               query.limit = query_int(req, "limit", 20, 1, 100);
               query.offset = query_int(req, "offset", 0, 0, 1'000'000);
               const auto page = repository.questions(query);
               sg::common::write_json(res, 200, page_json(page, [](const QuestionSummary& q) { return to_json(q); }));
             }));

  server.get("/api/v1/questions/:id", authenticated(verifier, [&repository](const auto& req, auto& res) {
               auto detail = repository.question(sg::common::path_id(req, "id"));
               if (!detail) throw sg::common::not_found("Question not found");
               sg::common::write_json(res, 200, to_json(*detail));
             }));
}

}  // namespace sg::content
