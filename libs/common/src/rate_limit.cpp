#include "sg/common/rate_limit.hpp"

#include <algorithm>
#include <vector>

namespace sg::common {

RateLimiter::RateLimiter(std::size_t max_events, std::chrono::seconds window, std::size_t max_keys)
    : max_events_(max_events), window_(window.count() > 0 ? window : std::chrono::seconds(1)),
      max_keys_(max_keys == 0 ? 1 : max_keys) {}

std::optional<std::chrono::seconds> RateLimiter::check(const std::string& key) { return check_at(key, Clock::now()); }

std::optional<std::chrono::seconds> RateLimiter::check_at(const std::string& key, Clock::time_point now) {
  if (max_events_ == 0) {
    return std::nullopt;
  }
  std::lock_guard lock(mutex_);
  if (++calls_since_sweep_ >= 1024 || windows_.size() >= max_keys_) {
    evict(now);
  }
  auto [it, inserted] = windows_.try_emplace(key, Window{now, 0});
  Window& w = it->second;
  if (!inserted && now - w.start >= window_) {
    w = Window{now, 0};
  }
  if (w.count < max_events_) {
    ++w.count;
    return std::nullopt;
  }
  const auto remaining = window_ - std::chrono::duration_cast<std::chrono::seconds>(now - w.start);
  return std::max(remaining, std::chrono::seconds(1));
}

std::optional<std::chrono::seconds> RateLimiter::retry_after(const std::string& key) const {
  return retry_after_at(key, Clock::now());
}

std::optional<std::chrono::seconds> RateLimiter::retry_after_at(const std::string& key, Clock::time_point now) const {
  if (max_events_ == 0) {
    return std::nullopt;
  }
  std::lock_guard lock(mutex_);
  const auto it = windows_.find(key);
  if (it == windows_.end() || now - it->second.start >= window_ || it->second.count < max_events_) {
    return std::nullopt;
  }
  const auto remaining = window_ - std::chrono::duration_cast<std::chrono::seconds>(now - it->second.start);
  return std::max(remaining, std::chrono::seconds(1));
}

std::size_t RateLimiter::tracked_keys() const {
  std::lock_guard lock(mutex_);
  return windows_.size();
}

void RateLimiter::evict(Clock::time_point now) {
  calls_since_sweep_ = 0;
  std::erase_if(windows_, [&](const auto& entry) { return now - entry.second.start >= window_; });
  if (windows_.size() < max_keys_) {
    return;
  }
  // Still full of live windows: drop the oldest half so memory stays bounded.
  std::vector<std::pair<Clock::time_point, std::string>> by_age;
  by_age.reserve(windows_.size());
  for (const auto& [key, w] : windows_) {
    by_age.emplace_back(w.start, key);
  }
  const auto half = by_age.begin() + static_cast<std::ptrdiff_t>(by_age.size() / 2);
  std::nth_element(by_age.begin(), half, by_age.end());
  for (auto it = by_age.begin(); it != half; ++it) {
    windows_.erase(it->second);
  }
}

}  // namespace sg::common
