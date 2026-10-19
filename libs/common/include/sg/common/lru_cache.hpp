#pragma once

/// @file lru_cache.hpp
/// Thread-safe, size-bounded LRU cache, sharded by key hash so concurrent requests rarely contend
/// on the same lock. Used to keep hot, rendered responses in process memory (cache-aside) in front
/// of PostgreSQL.

#include <array>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace sg::common {

template <class Key, class Value, std::size_t Shards = 16, class Hash = std::hash<Key>>
class LruCache {
  static_assert(Shards > 0);

 public:
  /// `capacity` entries in total, spread evenly across the shards (at least one per shard).
  explicit LruCache(std::size_t capacity) : per_shard_(capacity / Shards > 0 ? capacity / Shards : 1) {}

  /// The cached value, marking it most recently used.
  [[nodiscard]] std::optional<Value> get(const Key& key) {
    auto& shard = shard_for(key);
    std::lock_guard lock(shard.mutex);
    auto it = shard.index.find(key);
    if (it == shard.index.end()) {
      ++shard.misses;
      return std::nullopt;
    }
    shard.order.splice(shard.order.begin(), shard.order, it->second);
    ++shard.hits;
    return it->second->second;
  }

  /// Inserts or replaces `key`, evicting the shard's least recently used entry when full.
  void put(const Key& key, Value value) {
    auto& shard = shard_for(key);
    std::lock_guard lock(shard.mutex);
    if (auto it = shard.index.find(key); it != shard.index.end()) {
      it->second->second = std::move(value);
      shard.order.splice(shard.order.begin(), shard.order, it->second);
      return;
    }
    shard.order.emplace_front(key, std::move(value));
    shard.index.emplace(key, shard.order.begin());
    if (shard.order.size() > per_shard_) {
      shard.index.erase(shard.order.back().first);
      shard.order.pop_back();
    }
  }

  /// Cached value for `key`, or `load()` stored and returned. Concurrent misses for the same key
  /// may both call `load()`; the values must be interchangeable (true for cached reads of
  /// immutable data, which is what this is for).
  template <class Load>
  Value get_or_load(const Key& key, Load&& load) {
    if (auto hit = get(key)) return std::move(*hit);
    Value value = std::forward<Load>(load)();
    put(key, value);
    return value;
  }

  void clear() {
    for (auto& shard : shards_) {
      std::lock_guard lock(shard.mutex);
      shard.index.clear();
      shard.order.clear();
    }
  }

  struct Stats {
    std::size_t size = 0;
    std::size_t hits = 0;
    std::size_t misses = 0;
  };
  [[nodiscard]] Stats stats() const {
    Stats total;
    for (const auto& shard : shards_) {
      std::lock_guard lock(shard.mutex);
      total.size += shard.order.size();
      total.hits += shard.hits;
      total.misses += shard.misses;
    }
    return total;
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return per_shard_ * Shards; }

 private:
  struct Shard {
    mutable std::mutex mutex;
    std::list<std::pair<Key, Value>> order;  // most recently used first
    std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator, Hash> index;
    std::size_t hits = 0;
    std::size_t misses = 0;
  };

  Shard& shard_for(const Key& key) { return shards_[Hash{}(key) % Shards]; }

  std::size_t per_shard_;
  std::array<Shard, Shards> shards_;
};

}  // namespace sg::common
