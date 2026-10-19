#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sg::content {

// Read models returned by the repository (contracts §10.6). Timestamps are RFC 3339 UTC strings.

struct AuthorRef {
  std::int64_t id = 0;
  std::string display_name;
};

struct ActivityTotals {
  std::int64_t questions = 0;
  std::int64_t answers = 0;
  std::int64_t accepted_answers = 0;
  std::int64_t comments = 0;
  std::int64_t question_score = 0;
  std::int64_t answer_score = 0;
};

struct TagStat {
  std::string tag;
  std::int64_t posts = 0;
  std::int64_t score = 0;
};

struct BadgeAward {
  std::string name;
  int badge_class = 3;  // 1 gold, 2 silver, 3 bronze
  std::string awarded_at;
};

struct PostSummary {
  std::int64_t id = 0;
  bool is_question = true;
  std::int64_t question_id = 0;
  std::string title;
  std::int64_t score = 0;
  std::optional<std::int64_t> view_count;  // answers have no view count of their own
  std::string created_at;
  std::vector<std::string> tags;
};

struct Activity {
  std::int64_t user_id = 0;
  ActivityTotals totals;
  std::vector<TagStat> top_tags;
  std::int64_t gold = 0;
  std::int64_t silver = 0;
  std::int64_t bronze = 0;
  std::vector<BadgeAward> recent_badges;
  std::vector<PostSummary> top_posts;
};

struct TagInfo {
  std::string name;
  std::int64_t question_count = 0;
  std::optional<std::string> category;
};

struct QuestionSummary {
  std::int64_t id = 0;
  std::string title;
  std::vector<std::string> tags;
  std::int64_t score = 0;
  std::int64_t view_count = 0;
  std::int64_t answer_count = 0;
  std::string created_at;
  std::optional<AuthorRef> owner;
};

struct AnswerInfo {
  std::int64_t id = 0;
  std::optional<AuthorRef> owner;
  std::int64_t score = 0;
  std::string created_at;
  bool is_accepted = false;
};

struct QuestionDetail {
  QuestionSummary question;
  std::optional<std::int64_t> accepted_answer_id;
  std::optional<std::string> closed_at;
  std::int64_t comment_count = 0;
  std::vector<AnswerInfo> answers;
};

template <class T>
struct Page {
  std::vector<T> items;
  std::int64_t total = 0;
  std::int64_t limit = 0;
  std::int64_t offset = 0;
};

enum class QuestionSort { score, recent };

struct QuestionQuery {
  std::optional<std::string> tag;
  std::optional<std::string> text;  // full-text search over titles
  QuestionSort sort = QuestionSort::score;
  std::int64_t limit = 20;
  std::int64_t offset = 0;
};

// Port implemented by PostgresContentRepository (production) and by in-memory fakes (tests).
class ContentRepository {
 public:
  virtual ~ContentRepository() = default;
  virtual Activity activity(std::int64_t user_id) = 0;
  virtual Page<TagInfo> tags(const std::optional<std::string>& prefix, std::int64_t limit, std::int64_t offset) = 0;
  virtual Page<QuestionSummary> questions(const QuestionQuery& query) = 0;
  virtual std::optional<QuestionDetail> question(std::int64_t id) = 0;
  virtual bool ping() = 0;
};

}  // namespace sg::content
