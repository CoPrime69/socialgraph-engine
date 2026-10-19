#include "postgres_repository.hpp"
#include "seed_loader.hpp"

#include <sg/common/pg.hpp>
#include <sg/common/testing/test_env.hpp>

#include <gtest/gtest.h>

#include <memory>

namespace sg::content {
std::span<const sg::common::Migration> embedded_migrations();

namespace {

// Runs against a real PostgreSQL (SG_TEST_DATABASE_URL); each test gets its own throw-away
// database, so the suite can run in parallel with itself and with other suites.
class ContentPostgresTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto url = sg::common::testing::test_database_url();
    if (!url) GTEST_SKIP() << "SG_TEST_DATABASE_URL not set";
    database_ = std::make_unique<sg::common::testing::TempDatabase>(*url, "sg_content_test");
    sg::common::PgPoolOptions options;
    options.conninfo = database_->url();
    options.size = 4;
    pool_ = std::make_unique<sg::common::PgPool>(options);
    sg::common::run_migrations(*pool_, embedded_migrations(), 4242);
  }

  void TearDown() override {
    pool_.reset();  // close connections before the database is dropped
    database_.reset();
  }

  std::unique_ptr<sg::common::testing::TempDatabase> database_;
  std::unique_ptr<sg::common::PgPool> pool_;
};

TEST_F(ContentPostgresTest, SeedImportsOnceAndResolvesReferences) {
  auto report = load_seed_if_empty(*pool_, SG_CONTENT_FIXTURES);
  ASSERT_TRUE(report.loaded);
  EXPECT_EQ(report.authors, 2);
  EXPECT_EQ(report.questions, 3);
  EXPECT_EQ(report.question_tags, 4);
  EXPECT_EQ(report.answers, 3);
  EXPECT_EQ(report.comments, 2);   // the comment on an unknown post is skipped
  EXPECT_EQ(report.badges, 3);     // the badge of an unknown user is skipped
  EXPECT_EQ(report.user_tags, 4);
  EXPECT_FALSE(load_seed_if_empty(*pool_, SG_CONTENT_FIXTURES).loaded);

  PostgresContentRepository repo(*pool_);
  auto q2 = repo.question(2);
  ASSERT_TRUE(q2);
  EXPECT_FALSE(q2->accepted_answer_id) << "dangling accepted answer must be cleared";
  EXPECT_EQ(q2->closed_at, "2008-10-01T00:00:00Z");
}

TEST_F(ContentPostgresTest, ConstraintsRejectInvalidRows) {
  ASSERT_TRUE(load_seed_if_empty(*pool_, SG_CONTENT_FIXTURES).loaded);
  auto expect_violation = [&](const std::string& sql) {
    EXPECT_THROW(pool_->with_transaction([&](pqxx::work& tx) { tx.exec(sql); }), pqxx::sql_error) << sql;
  };
  expect_violation("INSERT INTO badges VALUES (99, 10, 'Bad', 4, false, now())");               // class
  expect_violation("INSERT INTO badges VALUES (99, 12345, 'Orphan', 3, false, now())");         // FK
  expect_violation("INSERT INTO answers VALUES (999, 424242, 10, now(), 0, 0, false)");         // FK
  expect_violation("INSERT INTO user_tags VALUES (10, 1, 0, 0)");                               // posts > 0
  expect_violation("INSERT INTO questions (id, created_at, title, score, view_count, answer_count, comment_count,"
                   " favorite_count) VALUES (9, now(), '', 0, 0, 0, 0, 0)");                    // empty title
  expect_violation("UPDATE questions SET accepted_answer_id = 123456 WHERE id = 1");          // deferred FK
}

TEST_F(ContentPostgresTest, ActivityAggregatesPerUser) {
  ASSERT_TRUE(load_seed_if_empty(*pool_, SG_CONTENT_FIXTURES).loaded);
  PostgresContentRepository repo(*pool_);
  auto a = repo.activity(10);
  EXPECT_EQ(a.totals.questions, 1);
  EXPECT_EQ(a.totals.question_score, 40);
  EXPECT_EQ(a.totals.answers, 1);
  EXPECT_EQ(a.totals.accepted_answers, 1);
  EXPECT_EQ(a.totals.answer_score, 25);
  EXPECT_EQ(a.totals.comments, 1);
  EXPECT_EQ(a.gold, 1);
  EXPECT_EQ(a.bronze, 1);
  ASSERT_EQ(a.top_tags.size(), 2U);
  EXPECT_EQ(a.top_tags[0].tag, "c#");
  ASSERT_EQ(a.recent_badges.size(), 2U);
  EXPECT_EQ(a.recent_badges[0].name, "c#");  // most recent first
  ASSERT_EQ(a.top_posts.size(), 2U);
  EXPECT_TRUE(a.top_posts[0].is_question);   // score 40 beats the 25-point answer
  EXPECT_EQ(a.top_posts[1].id, 101);
  EXPECT_EQ(a.top_posts[1].question_id, 1);
  EXPECT_EQ(a.top_posts[1].tags, (std::vector<std::string>{"javascript", "c#"}));

  auto nobody = repo.activity(424242);
  EXPECT_EQ(nobody.totals.questions, 0);
  EXPECT_TRUE(nobody.top_posts.empty());
}

TEST_F(ContentPostgresTest, TagsPrefixSearchTreatsWildcardsLiterally) {
  ASSERT_TRUE(load_seed_if_empty(*pool_, SG_CONTENT_FIXTURES).loaded);
  PostgresContentRepository repo(*pool_);
  auto all = repo.tags(std::nullopt, 10, 0);
  EXPECT_EQ(all.total, 3);
  EXPECT_EQ(all.items[0].name, "c#");
  EXPECT_EQ(all.items[0].category, ".NET & Windows");
  EXPECT_EQ(repo.tags(std::string("java"), 10, 0).total, 1);
  EXPECT_EQ(repo.tags(std::string("%"), 10, 0).total, 0);
  EXPECT_EQ(repo.tags(std::string("100%_"), 10, 0).total, 1);
  EXPECT_FALSE(repo.tags(std::string("100%_"), 10, 0).items[0].category);
  auto paged = repo.tags(std::nullopt, 1, 1);
  EXPECT_EQ(paged.total, 3);
  ASSERT_EQ(paged.items.size(), 1U);
  EXPECT_EQ(paged.items[0].name, "100%_legit");
}

TEST_F(ContentPostgresTest, QuestionsFilterSortAndSearch) {
  ASSERT_TRUE(load_seed_if_empty(*pool_, SG_CONTENT_FIXTURES).loaded);
  PostgresContentRepository repo(*pool_);
  QuestionQuery query;
  auto by_score = repo.questions(query);
  ASSERT_EQ(by_score.total, 3);
  EXPECT_EQ(by_score.items[0].id, 2);
  EXPECT_EQ(by_score.items[0].owner->display_name, "Ada");
  EXPECT_FALSE(by_score.items[2].owner);

  query.sort = QuestionSort::recent;
  EXPECT_EQ(repo.questions(query).items[0].id, 3);

  query = {};
  query.tag = "javascript";
  auto tagged = repo.questions(query);
  ASSERT_EQ(tagged.total, 1);
  EXPECT_EQ(tagged.items[0].tags, (std::vector<std::string>{"javascript", "c#"}));

  query.tag = "no-such-tag";
  EXPECT_EQ(repo.questions(query).total, 0);

  query = {};
  query.text = "parsing json";  // stemmed full-text match on "parse JSON"
  auto found = repo.questions(query);
  ASSERT_EQ(found.total, 1);
  EXPECT_EQ(found.items[0].id, 1);
  query.text = "'; DROP TABLE questions; --";
  EXPECT_EQ(repo.questions(query).total, 0);
  EXPECT_EQ(repo.questions({}).total, 3);
}

TEST_F(ContentPostgresTest, QuestionDetailListsAcceptedAnswerFirst) {
  ASSERT_TRUE(load_seed_if_empty(*pool_, SG_CONTENT_FIXTURES).loaded);
  PostgresContentRepository repo(*pool_);
  auto q = repo.question(1);
  ASSERT_TRUE(q);
  EXPECT_EQ(q->accepted_answer_id, 101);
  EXPECT_EQ(q->comment_count, 2);  // one on the question, one on its answer
  ASSERT_EQ(q->answers.size(), 2U);
  EXPECT_TRUE(q->answers[0].is_accepted);
  EXPECT_FALSE(q->answers[1].owner);
  EXPECT_FALSE(repo.question(404));
}

}  // namespace
}  // namespace sg::content
