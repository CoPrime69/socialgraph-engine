#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <sg/common/crypto.hpp>
#include <sg/common/csv.hpp>
#include <sg/graph/affinity.hpp>

#include "pg_fixture.hpp"

namespace {

namespace fs = std::filesystem;
using sg::graph_service::infra::seed_dataset_if_empty;

class SeedTest : public sg::graph_service::testing::GraphDatabaseTest {};

TEST_F(SeedTest, FixtureDatasetAndDemoMemberAreImported) {
  const auto report = seed_fixtures();
  ASSERT_TRUE(report.loaded);
  EXPECT_EQ(report.categories, 3U);
  EXPECT_EQ(report.branches, 4U);
  EXPECT_EQ(report.users, 31U);
  EXPECT_EQ(report.connections, 6U);
  EXPECT_EQ(report.demo_connections, 20U);
  EXPECT_EQ(report.graph_version, 1);
  EXPECT_EQ(repo_->graph_version(), 1);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM outbox"), 0);  // consumers bootstrap from the snapshot

  // Display order = file order.
  const auto taxonomy = repo_->load_taxonomy();
  ASSERT_EQ(taxonomy.categories.size(), 3U);
  EXPECT_EQ(taxonomy.categories[0].name, "Web Development");
  EXPECT_EQ(taxonomy.categories[2].slug, "systems");
  EXPECT_EQ(taxonomy.branches[3].name, "c++");

  // Imported profile with quoted, multi-byte and empty fields.
  const auto p103 = repo_->find_profile(103);
  ASSERT_TRUE(p103);
  EXPECT_EQ(p103->profile.about, "Writes \"quoted\", comma-separated text");
  EXPECT_EQ(p103->profile.website_url, "https://example.com/u/103");
  EXPECT_FALSE(p103->profile.location.has_value());
  EXPECT_EQ(repo_->find_profile(102)->profile.name, "Zo\xc3\xab Sch\xc3\xb6lly");
  EXPECT_FALSE(repo_->find_profile(202)->profile.last_access_at.has_value());
  EXPECT_DOUBLE_EQ(repo_->find_profile(202)->profile.influence, 45.5);

  // The demo member: connected (origin member) to the 20 most reputable javascript users, i.e. 104..123.
  const auto demo = repo_->find_profile(100000000);
  ASSERT_TRUE(demo);
  EXPECT_EQ(demo->profile.handle, "demo");
  EXPECT_EQ(demo->profile.name, "Demo Member");
  EXPECT_EQ(demo->profile.category, "Web Development");
  EXPECT_EQ(demo->profile.branch, "javascript");
  EXPECT_EQ(demo->profile.reputation, 1);
  EXPECT_EQ(demo->connection_count, 20);
  const auto connections = repo_->list_connections(100000000);
  ASSERT_EQ(connections.items.size(), 20U);
  for (const auto& c : connections.items) {
    EXPECT_GE(c.user.id, 104);
    EXPECT_LE(c.user.id, 123);
    EXPECT_EQ(c.origin, sg::graph_service::domain::ConnectionOrigin::member);
    EXPECT_EQ(c.interactions, 0);
    // strength = affinity(demo, user): same category and branch (codes 0/0), influence 0 vs 50 + (id - 101).
    const double influence = 50.0 + static_cast<double>(c.user.id - 101);
    EXPECT_DOUBLE_EQ(c.strength, sg::graph::connection_strength({0, 0, 0.0}, {0, 0, influence})) << c.user.id;
  }
  EXPECT_EQ(connections.items.front().user.id, 104);  // lowest influence difference first
}

TEST_F(SeedTest, SeedingIsIdempotentAndSafeUnderConcurrency) {
  std::vector<std::thread> threads;
  std::atomic<int> loaded{0};
  for (int i = 0; i < 3; ++i) {
    threads.emplace_back([&] {
      auto pool = make_pool(2);
      if (seed_dataset_if_empty(*pool, sg::graph_service::testing::fixtures_dir()).loaded) {
        loaded.fetch_add(1);
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(loaded.load(), 1);
  EXPECT_FALSE(seed_fixtures().loaded);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM profiles"), 32);
  EXPECT_EQ(repo_->graph_version(), 1);
}

class BrokenDatasetTest : public SeedTest {
 protected:
  void SetUp() override {
    SeedTest::SetUp();
    if (IsSkipped()) {
      return;
    }
    dir_ = fs::temp_directory_path() / ("sg_graph_seed_" + sg::common::random_hex(6));
    fs::create_directories(dir_);
    for (const char* name : {"categories.csv", "branches.csv", "users.csv", "connections.csv"}) {
      fs::copy_file(fs::path(sg::graph_service::testing::fixtures_dir()) / name, dir_ / name);
    }
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(dir_, ec);
  }
  void append(const char* file, const std::string& line) {
    std::ofstream out(dir_ / file, std::ios::app);
    out << line << "\n";
  }
  /// Seeding fails with a message naming file and line, and leaves the database untouched.
  void expect_rejected(const std::string& file_and_line) {
    try {
      (void)seed_dataset_if_empty(*pool_, dir_);
      ADD_FAILURE() << "broken dataset accepted";
    } catch (const std::exception& e) {
      EXPECT_NE(std::string(e.what()).find(file_and_line), std::string::npos) << e.what();
    }
    EXPECT_EQ(scalar<long>("SELECT count(*) FROM categories"), 0);
    EXPECT_EQ(scalar<long>("SELECT count(*) FROM profiles"), 0);
    EXPECT_EQ(repo_->graph_version(), 0);
  }
  fs::path dir_;
};

TEST_F(BrokenDatasetTest, UnknownBranchIsRejectedWithItsLine) {
  append("users.csv", "999,bad_branch_999,Bad,1,1.00,2008-08-01T00:00:00Z,,,,,0,0,0,Web Development,cobol");
  expect_rejected("users.csv:33: unknown branch 'cobol'");
}

TEST_F(BrokenDatasetTest, DanglingConnectionIsRejected) {
  append("connections.csv", "101,999,1,2008-08-01T00:00:00Z,,0.5");
  expect_rejected("connections.csv:8: connection references an unknown user");
}

TEST_F(BrokenDatasetTest, MalformedValuesAreRejected) {
  append("connections.csv", "101,103,1,yesterday,,0.5");
  expect_rejected("connections.csv:8: first_interaction_at must be an RFC 3339 UTC timestamp");
}

TEST_F(BrokenDatasetTest, MissingFileIsRejected) {
  fs::remove(dir_ / "connections.csv");
  expect_rejected("dataset file not found");
}

TEST_F(SeedTest, RealStackOverflowDatasetLoadsInSeconds) {
  const fs::path dir = SG_DATASET_DIR;
  if (!fs::exists(dir / "users.csv.gz")) {
    GTEST_SKIP() << "dataset not found in " << dir;
  }
  const auto report = seed_dataset_if_empty(*pool_, dir);
  ASSERT_TRUE(report.loaded);
  EXPECT_EQ(report.categories, 10U);
  EXPECT_EQ(report.branches, 73U);
  EXPECT_EQ(report.users, 18648U);
  EXPECT_EQ(report.connections, 204650U);
  EXPECT_EQ(report.demo_connections, 20U);
  EXPECT_EQ(repo_->graph_version(), 1);
  EXPECT_LT(report.seconds, 60.0);
  std::cout << "[ INFO     ] real dataset imported in " << report.seconds << " s\n";
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM connections"), 204670);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM connections c JOIN profiles p ON p.user_id = c.user_a "
                         "WHERE c.user_b = 100000000 AND p.branch = 'javascript'"),
            20);
  const auto snapshot = repo_->snapshot();
  EXPECT_EQ(snapshot.users.size(), 18649U);
  EXPECT_EQ(snapshot.edges.size(), 204670U);
}

}  // namespace
