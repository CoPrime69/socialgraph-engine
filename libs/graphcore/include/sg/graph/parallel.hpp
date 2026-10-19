#pragma once

/// @file parallel.hpp
/// Minimal fork–join loop used by the parallel algorithms (no global thread pool).

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <system_error>
#include <thread>
#include <type_traits>
#include <vector>

namespace sg::graph {

/// Worker count for a request of `threads`: the value itself, or the hardware concurrency
/// (at least 1) when `threads` is 0. Complexity: O(1).
[[nodiscard]] inline unsigned resolve_thread_count(unsigned threads) noexcept {
  if (threads != 0) {
    return threads;
  }
  const unsigned hardware = std::thread::hardware_concurrency();
  return hardware == 0 ? 1U : hardware;
}

/// Calls `body(i, worker)` for every i in [begin, end) on up to `threads` threads
/// (0 = hardware concurrency).
///
/// * Dynamic scheduling: workers claim the next index from a shared atomic counter, so uneven
///   iterations balance themselves. Use one index per chunk of work when iterations are tiny.
/// * `worker` is in [0, number of workers) and is unique among concurrently running calls, so it
///   can index per-thread scratch buffers. The calling thread participates as worker 0; with one
///   worker (or one index) everything runs inline on the caller and no thread is started.
/// * `body` may also take only the index: `body(i)`.
/// * Threads are std::jthread, joined before returning. If starting a thread fails the loop
///   continues with the workers it has.
/// * If `body` throws, no further indices are handed out, all workers are joined and the first
///   exception is rethrown to the caller.
///
/// `body` is invoked concurrently and must be safe to call from several threads.
/// Complexity: O(end − begin) calls plus O(threads) thread start-ups.
template <class F>
void parallel_for(std::size_t begin, std::size_t end, unsigned threads, F&& body) {
  auto call = [&body](std::size_t i, unsigned worker) {
    if constexpr (std::is_invocable_v<F&, std::size_t, unsigned>) {
      body(i, worker);
    } else {
      static_assert(std::is_invocable_v<F&, std::size_t>,
                    "parallel_for body must be callable as body(i, worker) or body(i)");
      body(i);
    }
  };
  if (end <= begin) {
    return;
  }
  const std::size_t count = end - begin;
  const auto workers =
      static_cast<unsigned>(std::min<std::size_t>(resolve_thread_count(threads), count));
  if (workers <= 1) {
    for (std::size_t i = begin; i < end; ++i) {
      call(i, 0U);
    }
    return;
  }

  std::atomic<std::size_t> next{begin};
  std::atomic<bool> stop{false};
  std::mutex error_mutex;
  std::exception_ptr error;

  auto run = [&](unsigned worker) {
    try {
      while (!stop.load(std::memory_order_relaxed)) {
        const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
        if (i >= end) {
          break;
        }
        call(i, worker);
      }
    } catch (...) {
      const std::lock_guard<std::mutex> lock(error_mutex);
      if (!error) {
        error = std::current_exception();
      }
      stop.store(true, std::memory_order_relaxed);
    }
  };

  {
    std::vector<std::jthread> pool;
    pool.reserve(workers - 1);
    for (unsigned worker = 1; worker < workers; ++worker) {
      try {
        pool.emplace_back(run, worker);
      } catch (const std::system_error&) {
        break;  // could not start another thread: carry on with the ones we have
      }
    }
    run(0U);
  }  // std::jthread joins on destruction

  if (error) {
    std::rethrow_exception(error);
  }
}

}  // namespace sg::graph
