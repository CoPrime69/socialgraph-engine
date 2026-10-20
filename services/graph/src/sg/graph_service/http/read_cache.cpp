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

std::shared_ptr<const std::string> ReadCache::page_body(const std::vector<const std::string*>& items,
                                                        std::int64_t total, std::int64_t limit, std::int64_t offset) {
  // Same bytes as rendering domain::to_json(UserPage): nlohmann::json orders object keys, and each
  // item is the member's profile body.
  std::size_t size = 64;
  for (const auto* item : items) {
    size += item->size() + 1;
  }
  std::string body;
  body.reserve(size);
  body += "{\"items\":[";
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i > 0) {
      body += ',';
    }
    body += *items[i];
  }
  body += "],\"limit\":" + std::to_string(limit) + ",\"offset\":" + std::to_string(offset) +
          ",\"total\":" + std::to_string(total) + "}";
  return std::make_shared<const std::string>(std::move(body));
}

std::shared_ptr<const std::string> ReadCache::search(const std::string& key, const std::function<LoadedSearch()>& load,
                                                     const std::function<std::string(std::int64_t)>& render_profile) {
  const auto now = Clock::now();
  if (searches_) {
    if (auto hit = searches_->get(key); hit && hit->expires_at > now) {
      std::vector<std::shared_ptr<const std::string>> profiles;
      std::vector<const std::string*> items;
      profiles.reserve(hit->ids->size());
      items.reserve(hit->ids->size());
      for (const auto id : *hit->ids) {
        profiles.push_back(member(Kind::profile, id, [&] { return render_profile(id); }));
        items.push_back(profiles.back().get());
      }
      return page_body(items, hit->total, hit->limit, hit->offset);
    }
  }
  const auto loaded = load();
  std::vector<const std::string*> items;
  items.reserve(loaded.items.size());
  for (const auto& item : loaded.items) {
    items.push_back(&item.second);
  }
  if (searches_) {
    auto ids = std::make_shared<std::vector<std::int64_t>>();
    ids->reserve(loaded.items.size());
    for (const auto& item : loaded.items) {
      ids->push_back(item.first);
    }
    searches_->put(key, {std::move(ids), loaded.total, loaded.limit, loaded.offset, now + options_.search_ttl});
  }
  return page_body(items, loaded.total, loaded.limit, loaded.offset);
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
