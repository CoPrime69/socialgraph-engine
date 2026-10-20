#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

#include "sg/graph/parallel.hpp"

namespace sg::graph {
namespace {

TEST(ParallelFor, VisitsEveryIndexExactlyOnce) {
  for (const unsigned threads : {0U, 1U, 2U, 3U, 4U, 8U, 64U}) {
    std::vector<std::atomic<int>> hits(1000);
    parallel_for(100, 1100, threads, [&](std::size_t i, unsigned) { hits[i - 100].fetch_add(1); });
    for (const auto& h : hits) {
      ASSERT_EQ(h.load(), 1) << "threads " << threads;
    }
  }
}

TEST(ParallelFor, WorkerIndicesAreDenseAndBounded) {
  constexpr unsigned kThreads = 4;
  std::vector<std::atomic<int>> per_worker(kThreads);
  std::atomic<unsigned> max_worker{0};
  parallel_for(0, 10000, kThreads, [&](std::size_t, unsigned worker) {
    per_worker.at(worker).fetch_add(1);
    unsigned seen = max_worker.load();
    while (worker > seen && !max_worker.compare_exchange_weak(seen, worker)) {
    }
  });
  EXPECT_LT(max_worker.load(), kThreads);
  int total = 0;
  for (const auto& w : per_worker) {
    total += w.load();
  }
  EXPECT_EQ(total, 10000);
}

TEST(ParallelFor, EmptyAndReversedRangesDoNothing) {
  int calls = 0;
  parallel_for(5, 5, 4, [&](std::size_t, unsigned) { ++calls; });
  parallel_for(7, 3, 4, [&](std::size_t, unsigned) { ++calls; });
  EXPECT_EQ(calls, 0);
}

TEST(ParallelFor, SingleWorkerRunsInlineInOrder) {
  std::vector<std::size_t> order;
  parallel_for(0, 5, 1, [&](std::size_t i, unsigned worker) {
    EXPECT_EQ(worker, 0U);
    order.push_back(i);  // no synchronisation needed: runs on the caller
  });
  EXPECT_EQ(order, (std::vector<std::size_t>{0, 1, 2, 3, 4}));
}

TEST(ParallelFor, AcceptsIndexOnlyBodies) {
  std::atomic<std::size_t> sum{0};
  parallel_for(0, 101, 3, [&](std::size_t i) { sum.fetch_add(i); });
  EXPECT_EQ(sum.load(), 5050U);
}

TEST(ParallelFor, PropagatesTheFirstExceptionAndStops) {
  // Every iteration sleeps, so draining all indices takes seconds while the throw at index 17 is
  // caught within microseconds; with empty bodies the other workers could finish every index
  // before the exception is unwound, and the test would depend on the scheduler.
  std::atomic<int> calls{0};
  EXPECT_THROW(parallel_for(0, 100000, 4,
                            [&](std::size_t i, unsigned) {
                              calls.fetch_add(1);
                              if (i == 17) {
                                throw std::runtime_error("boom");
                              }
                              std::this_thread::sleep_for(std::chrono::microseconds(50));
                            }),
               std::runtime_error);
  EXPECT_LT(calls.load(), 100000);  // remaining indices are abandoned

  try {
    parallel_for(0, 8, 1, [](std::size_t i, unsigned) {
      if (i == 3) {
        throw std::logic_error("inline");
      }
    });
    FAIL() << "expected an exception";
  } catch (const std::logic_error& e) {
    EXPECT_STREQ(e.what(), "inline");
  }
}

TEST(ParallelFor, ResolveThreadCount) {
  EXPECT_EQ(resolve_thread_count(3), 3U);
  EXPECT_GE(resolve_thread_count(0), 1U);
}

}  // namespace
}  // namespace sg::graph
