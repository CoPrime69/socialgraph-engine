#include "sg/graph_service/http/read_cache.hpp"

namespace sg::graph_service::http {

ReadCache::ReadCache(ReadCacheOptions options) : options_(options) {
  if (options_.member_entries > 0) {
    members_ = std::make_unique<sg::common::LruCache<std::int64_t, std::shared_ptr<const std::string>>>(
        options_.member_entries);
  }
  if (options_.search_entries > 0 && options_.search_ttl.count() > 0) {
    searches_ = std::make_unique<sg::common::LruCache<std::string, SearchEntry>>(options_.search_entries);
  }
}

std::shared_ptr<const std::string> ReadCache::member(Kind kind, std::int64_t member_id,
                                                     const std::function<std::string()>& render) {
  if (!members_) {
    return std::make_shared<const std::string>(render());
  }
  const auto k = key(kind, member_id);
  if (auto hit = members_->get(k)) {
    return *hit;
  }
  // Read the generations before loading: if an invalidation (or clear) happens while we load, the
  // value may predate the write that caused it, so it must not be stored.
  auto& gen = generation(member_id);
  const auto before = gen.load(std::memory_order_acquire);
  const auto clears_before = clears_.load(std::memory_order_acquire);
  auto body = std::make_shared<const std::string>(render());
  // Checked under the shard lock: invalidate() bumps the generation before it erases (under the
  // same lock), so either we see the bump and skip the put, or its erase removes what we put.
  members_->put_if(k, body, [&] {
    return gen.load(std::memory_order_acquire) == before && clears_.load(std::memory_order_acquire) == clears_before;
  });
  return body;
}

std::shared_ptr<const std::string> ReadCache::search(const std::string& key,
                                                     const std::function<std::string()>& render) {
  if (!searches_) {
    return std::make_shared<const std::string>(render());
  }
  const auto now = Clock::now();
  if (auto hit = searches_->get(key); hit && hit->expires_at > now) {
    return hit->body;
  }
  auto body = std::make_shared<const std::string>(render());
  searches_->put(key, {body, now + options_.search_ttl});
  return body;
}

void ReadCache::invalidate(std::int64_t member_id) {
  if (!members_) {
    return;
  }
  generation(member_id).fetch_add(1, std::memory_order_acq_rel);
  members_->erase(key(Kind::profile, member_id));
  members_->erase(key(Kind::connections, member_id));
}

void ReadCache::clear() {
  clears_.fetch_add(1, std::memory_order_acq_rel);
  if (members_) {
    members_->clear();
  }
  if (searches_) {
    searches_->clear();
  }
}

ReadCache::Stats ReadCache::stats() const {
  Stats stats;
  if (members_) {
    const auto s = members_->stats();
    stats.member_hits = s.hits;
    stats.member_misses = s.misses;
  }
  if (searches_) {
    const auto s = searches_->stats();
    stats.search_hits = s.hits;
    stats.search_misses = s.misses;
  }
  return stats;
}

}  // namespace sg::graph_service::http
