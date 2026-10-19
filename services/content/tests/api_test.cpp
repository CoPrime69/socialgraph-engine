#include "api.hpp"

#include <sg/common/auth.hpp>
#include <sg/common/http.hpp>

#include <gtest/gtest.h>
#include <httplib.h>

#include <memory>
#include <mutex>
#include <thread>

namespace sg::content {
namespace {

using nlohmann::json;

// In-memory ContentRepository that records the last query it was given, so the tests can check
// what the HTTP layer forwards after validation.
class FakeRepository : public ContentRepository {
 public:
  Activity activity(std::int64_t user_id) override {
    Activity a;
    a.user_id = user_id;
    a.totals = {.questions = 2, .answers = 5, .accepted_answers = 1, .comments = 3, .question_score = 7, .answer_score = 12};
    a.top_tags = {{"c++", 4, 10}, {"stl", 1, 2}};
    a.gold = 0;
    a.silver = 1;
    a.bronze = 4;
    a.recent_badges = {{"Teacher", 3, "2008-09-15T10:00:00Z"}};
    PostSummary q{.id = 11, .is_question = true, .question_id = 11, .title = "Why is std::vector fast?", .score = 7,
                  .view_count = 1200, .created_at = "2008-08-02T12:00:00Z", .tags = {"c++", "stl"}};
    PostSummary ans{.id = 99, .is_question = false, .question_id = 42, .title = "How do templates work?", .score = 5,
                    .view_count = std::nullopt, .created_at = "2008-08-05T12:00:00Z", .tags = {"c++"}};
    a.top_posts = {q, ans};
    return a;
  }

  Page<TagInfo> tags(const std::optional<std::string>& prefix, std::int64_t limit, std::int64_t offset) override {
    std::lock_guard lock(mutex_);
    last_prefix = prefix;
    Page<TagInfo> page;
    page.items = {{"c++", 1500, "systems"}, {"c#-to-vb.net", 3, std::nullopt}};
    page.total = 2;
    page.limit = limit;
    page.offset = offset;
    return page;
  }

  Page<QuestionSummary> questions(const QuestionQuery& query) override {
    std::lock_guard lock(mutex_);
    last_query = query;
    Page<QuestionSummary> page;
    page.items = {summary(42)};
    page.total = 1;
    page.limit = query.limit;
    page.offset = query.offset;
    return page;
  }

  std::optional<QuestionDetail> question(std::int64_t id) override {
    if (id != 42) return std::nullopt;
    QuestionDetail d;
    d.question = summary(42);
    d.accepted_answer_id = 99;
    d.comment_count = 4;
    d.answers = {{99, AuthorRef{20, "Bob"}, 5, "2008-08-05T12:00:00Z", true},
                 {100, std::nullopt, 1, "2008-08-06T12:00:00Z", false}};
    return d;
  }

  bool ping() override { return true; }

  static QuestionSummary summary(std::int64_t id) {
    return {.id = id, .title = "How do templates work?", .tags = {"c++", "templates"}, .score = 9, .view_count = 3000,
            .answer_count = 2, .created_at = "2008-08-04T09:30:00Z", .owner = AuthorRef{10, "Alice"}};
  }

  std::mutex mutex_;
  std::optional<std::string> last_prefix;
  QuestionQuery last_query;
};

class ContentApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    keys_ = sg::common::generate_keypair();
    issuer_ = std::make_unique<sg::common::TokenIssuer>(keys_.seed_b64, std::chrono::hours(1));
    verifier_ = std::make_unique<sg::common::TokenVerifier>(keys_.public_key_b64);
    sg::common::HttpServerOptions options;
    options.threads = 2;
    server_ = std::make_unique<sg::common::HttpServer>("content-test", options);
    register_routes(*server_, repository_, *verifier_);
    port_ = server_->bind_to_any_port();
    thread_ = std::jthread([this] { server_->listen(); });
    ASSERT_TRUE(server_->wait_until_ready());
    client_ = std::make_unique<httplib::Client>("127.0.0.1", port_);
    token_ = issuer_->issue(100000000, "demo", "Demo User").token;
  }

  void TearDown() override {
    server_->stop();
    thread_.join();
  }

  httplib::Result get(const std::string& path, bool auth = true) {
    httplib::Headers headers;
    if (auth) headers.emplace("Authorization", "Bearer " + token_);
    return client_->Get(path, headers);
  }

  static json body(const httplib::Result& r) { return json::parse(r->body); }

  void expect_error(const std::string& path, int status, const std::string& code) {
    auto r = get(path);
    ASSERT_TRUE(r) << path;
    EXPECT_EQ(r->status, status) << path;
    EXPECT_EQ(body(r)["error"]["code"], code) << path;
  }

  FakeRepository repository_;
  sg::common::KeyPair keys_;
  std::unique_ptr<sg::common::TokenIssuer> issuer_;
  std::unique_ptr<sg::common::TokenVerifier> verifier_;
  std::unique_ptr<sg::common::HttpServer> server_;
  int port_ = 0;
  std::jthread thread_;
  std::unique_ptr<httplib::Client> client_;
  std::string token_;
};

TEST_F(ContentApiTest, EveryRouteRequiresAuthentication) {
  for (const std::string path : {"/api/v1/users/10/activity", "/api/v1/tags", "/api/v1/questions", "/api/v1/questions/42"}) {
    auto r = get(path, false);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 401) << path;
    EXPECT_EQ(body(r)["error"]["code"], "unauthenticated") << path;
  }
  // A token signed by a different key is rejected too.
  sg::common::TokenIssuer other(sg::common::generate_keypair().seed_b64, std::chrono::hours(1));
  auto r = client_->Get("/api/v1/tags", {{"Authorization", "Bearer " + other.issue(1, "x", "X").token}});
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 401);
}

TEST_F(ContentApiTest, ActivityShape) {
  auto r = get("/api/v1/users/10/activity");
  ASSERT_TRUE(r);
  ASSERT_EQ(r->status, 200);
  auto b = body(r);
  EXPECT_EQ(b["user_id"], 10);
  EXPECT_EQ(b["totals"]["answers"], 5);
  EXPECT_EQ(b["totals"]["answer_score"], 12);
  EXPECT_EQ(b["top_tags"][0]["tag"], "c++");
  EXPECT_EQ(b["badges"]["bronze"], 4);
  EXPECT_EQ(b["badges"]["recent"][0]["class"], 3);
  ASSERT_EQ(b["top_posts"].size(), 2u);
  EXPECT_EQ(b["top_posts"][0]["type"], "question");
  EXPECT_EQ(b["top_posts"][0]["url"], "https://stackoverflow.com/q/11");
  EXPECT_EQ(b["top_posts"][1]["type"], "answer");
  EXPECT_EQ(b["top_posts"][1]["question_id"], 42);
  EXPECT_TRUE(b["top_posts"][1]["view_count"].is_null());
  EXPECT_EQ(b["top_posts"][1]["url"], "https://stackoverflow.com/a/99");
}

TEST_F(ContentApiTest, ActivityRejectsInvalidIds) {
  expect_error("/api/v1/users/abc/activity", 404, "not_found");
  expect_error("/api/v1/users/0/activity", 404, "not_found");
  expect_error("/api/v1/users/-5/activity", 404, "not_found");
}

TEST_F(ContentApiTest, TagsForwardTrimmedPrefixAndPaging) {
  auto r = get("/api/v1/tags?q=%20c%2B%2B%20&limit=5&offset=10");
  ASSERT_TRUE(r);
  ASSERT_EQ(r->status, 200);
  auto b = body(r);
  EXPECT_EQ(b["limit"], 5);
  EXPECT_EQ(b["offset"], 10);
  EXPECT_EQ(b["items"][0]["category"], "systems");
  EXPECT_TRUE(b["items"][1]["category"].is_null());
  EXPECT_EQ(repository_.last_prefix, std::optional<std::string>("c++"));

  ASSERT_EQ(get("/api/v1/tags?q=%20%20")->status, 200);
  EXPECT_FALSE(repository_.last_prefix.has_value()) << "blank prefix means no filter";
}

TEST_F(ContentApiTest, QuestionsValidation) {
  expect_error("/api/v1/questions?sort=votes", 400, "bad_request");
  expect_error("/api/v1/questions?limit=0", 400, "bad_request");
  expect_error("/api/v1/questions?limit=101", 400, "bad_request");
  expect_error("/api/v1/questions?limit=ten", 400, "bad_request");
  expect_error("/api/v1/questions?offset=-1", 400, "bad_request");
  expect_error("/api/v1/questions?q=" + std::string(101, 'a'), 400, "bad_request");
  expect_error("/api/v1/questions?q=a%01b", 400, "bad_request");
  expect_error("/api/v1/tags?q=" + std::string(101, 'a'), 400, "bad_request");
}

TEST_F(ContentApiTest, QuestionsForwardQuery) {
  auto r = get("/api/v1/questions?tag=c%2B%2B&q=templates&sort=recent&limit=3&offset=6");
  ASSERT_TRUE(r);
  ASSERT_EQ(r->status, 200);
  const auto& q = repository_.last_query;
  EXPECT_EQ(q.tag, std::optional<std::string>("c++"));
  EXPECT_EQ(q.text, std::optional<std::string>("templates"));
  EXPECT_EQ(q.sort, QuestionSort::recent);
  EXPECT_EQ(q.limit, 3);
  EXPECT_EQ(q.offset, 6);

  auto b = body(r);
  EXPECT_EQ(b["total"], 1);
  EXPECT_EQ(b["items"][0]["owner"]["display_name"], "Alice");
  EXPECT_EQ(b["items"][0]["url"], "https://stackoverflow.com/q/42");

  ASSERT_EQ(get("/api/v1/questions")->status, 200);
  EXPECT_EQ(repository_.last_query.sort, QuestionSort::score);
  EXPECT_EQ(repository_.last_query.limit, 20);
  EXPECT_FALSE(repository_.last_query.tag.has_value());
}

TEST_F(ContentApiTest, QuestionDetail) {
  auto r = get("/api/v1/questions/42");
  ASSERT_TRUE(r);
  ASSERT_EQ(r->status, 200);
  auto b = body(r);
  EXPECT_EQ(b["id"], 42);
  EXPECT_EQ(b["accepted_answer_id"], 99);
  EXPECT_TRUE(b["closed_at"].is_null());
  EXPECT_EQ(b["comment_count"], 4);
  ASSERT_EQ(b["answers"].size(), 2u);
  EXPECT_TRUE(b["answers"][0]["is_accepted"].get<bool>());
  EXPECT_EQ(b["answers"][0]["url"], "https://stackoverflow.com/a/99");
  EXPECT_TRUE(b["answers"][1]["owner"].is_null()) << "deleted users have no owner";

  expect_error("/api/v1/questions/7", 404, "not_found");
}

}  // namespace
}  // namespace sg::content
