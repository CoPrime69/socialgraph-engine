#include "sg/common/redis.hpp"

#include <csignal>
#include <mutex>
#include <string_view>

#include <hiredis/hiredis.h>

namespace sg::common {
namespace {

struct ContextDeleter {
  void operator()(redisContext* ctx) const noexcept { redisFree(ctx); }
};
struct ReplyDeleter {
  void operator()(redisReply* reply) const noexcept { freeReplyObject(reply); }
};
using ContextPtr = std::unique_ptr<redisContext, ContextDeleter>;
using ReplyPtr = std::unique_ptr<redisReply, ReplyDeleter>;

timeval to_timeval(std::chrono::milliseconds ms) {
  timeval tv{};
  tv.tv_sec = static_cast<decltype(tv.tv_sec)>(ms.count() / 1000);
  tv.tv_usec = static_cast<decltype(tv.tv_usec)>((ms.count() % 1000) * 1000);
  return tv;
}

std::string reply_string(const redisReply* reply) {
  if (reply == nullptr || (reply->type != REDIS_REPLY_STRING && reply->type != REDIS_REPLY_STATUS &&
                           reply->type != REDIS_REPLY_VERB)) {
    throw RedisError("redis: unexpected reply type");
  }
  return std::string(reply->str, reply->len);
}

/// Parses one stream entry: [id, [field, value, ...] | nil].
StreamEntry parse_entry(const redisReply* entry) {
  if (entry == nullptr || entry->type != REDIS_REPLY_ARRAY || entry->elements < 2) {
    throw RedisError("redis: malformed stream entry");
  }
  StreamEntry out;
  out.id = reply_string(entry->element[0]);
  const redisReply* fields = entry->element[1];
  if (fields != nullptr && fields->type == REDIS_REPLY_ARRAY) {
    for (std::size_t i = 0; i + 1 < fields->elements; i += 2) {
      out.fields.insert_or_assign(reply_string(fields->element[i]), reply_string(fields->element[i + 1]));
    }
  }
  // A nil field list means the entry was deleted/trimmed while pending; it keeps its id.
  return out;
}

std::vector<StreamEntry> parse_entries(const redisReply* list) {
  std::vector<StreamEntry> out;
  if (list == nullptr || list->type == REDIS_REPLY_NIL) {
    return out;
  }
  if (list->type != REDIS_REPLY_ARRAY) {
    throw RedisError("redis: malformed entry list");
  }
  out.reserve(list->elements);
  for (std::size_t i = 0; i < list->elements; ++i) {
    out.push_back(parse_entry(list->element[i]));
  }
  return out;
}

/// XREAD / XREADGROUP reply: nil (timeout) or [[stream, [entries...]], ...] for one stream.
std::vector<StreamEntry> parse_read_reply(const redisReply* reply) {
  if (reply == nullptr || reply->type == REDIS_REPLY_NIL) {
    return {};
  }
  if (reply->type != REDIS_REPLY_ARRAY) {
    throw RedisError("redis: malformed XREAD reply");
  }
  std::vector<StreamEntry> out;
  for (std::size_t i = 0; i < reply->elements; ++i) {
    const redisReply* stream = reply->element[i];
    if (stream == nullptr || stream->type != REDIS_REPLY_ARRAY || stream->elements < 2) {
      throw RedisError("redis: malformed XREAD stream reply");
    }
    auto entries = parse_entries(stream->element[1]);
    out.insert(out.end(), std::make_move_iterator(entries.begin()), std::make_move_iterator(entries.end()));
  }
  return out;
}

void ignore_sigpipe_once() {
  static std::once_flag once;
  // hiredis writes with plain write(); a peer reset must surface as EPIPE, not kill the process.
  std::call_once(once, [] { std::signal(SIGPIPE, SIG_IGN); });
}

}  // namespace

struct RedisClient::Impl {
  RedisOptions options;
  ContextPtr ctx;
  std::chrono::milliseconds current_timeout{-1};

  void connect() {
    ignore_sigpipe_once();
    ContextPtr fresh(redisConnectWithTimeout(options.host.c_str(), options.port, to_timeval(options.timeout)));
    if (!fresh) {
      throw RedisError("redis: cannot allocate context");
    }
    if (fresh->err != 0) {
      throw RedisError("redis: cannot connect to " + options.host + ":" + std::to_string(options.port) + ": " +
                       fresh->errstr);
    }
    redisEnableKeepAlive(fresh.get());
    ctx = std::move(fresh);
    current_timeout = std::chrono::milliseconds(-1);
  }

  ReplyPtr command(const std::vector<std::string>& args, std::chrono::milliseconds extra_timeout = {}) {
    if (!ctx) {
      connect();
    }
    const auto timeout = options.timeout + extra_timeout;
    if (timeout != current_timeout) {
      if (redisSetTimeout(ctx.get(), to_timeval(timeout)) != REDIS_OK) {
        ctx.reset();
        throw RedisError("redis: cannot set timeout");
      }
      current_timeout = timeout;
    }
    std::vector<const char*> argv;
    std::vector<std::size_t> argvlen;
    argv.reserve(args.size());
    argvlen.reserve(args.size());
    for (const auto& arg : args) {
      argv.push_back(arg.data());
      argvlen.push_back(arg.size());
    }
    ReplyPtr reply(static_cast<redisReply*>(
        redisCommandArgv(ctx.get(), static_cast<int>(argv.size()), argv.data(), argvlen.data())));
    if (!reply) {
      const std::string error = ctx->errstr;
      ctx.reset();  // the connection state is unknown after an I/O error; reconnect next time
      throw RedisError("redis: " + args.front() + " failed: " + error);
    }
    if (reply->type == REDIS_REPLY_ERROR) {
      throw RedisError("redis: " + args.front() + ": " + std::string(reply->str, reply->len));
    }
    return reply;
  }
};

RedisClient::RedisClient(RedisOptions options) : impl_(std::make_unique<Impl>()) {
  impl_->options = std::move(options);
}

RedisClient::~RedisClient() = default;

std::string RedisClient::xadd(const std::string& stream,
                              const std::vector<std::pair<std::string, std::string>>& fields) {
  return xadd(stream, fields, 0);
}

std::string RedisClient::xadd(const std::string& stream,
                              const std::vector<std::pair<std::string, std::string>>& fields,
                              std::size_t approx_max_len) {
  if (fields.empty()) {
    throw std::invalid_argument("xadd requires at least one field");
  }
  std::vector<std::string> args{"XADD", stream};
  if (approx_max_len > 0) {
    args.insert(args.end(), {"MAXLEN", "~", std::to_string(approx_max_len)});
  }
  args.emplace_back("*");
  for (const auto& [field, value] : fields) {
    args.push_back(field);
    args.push_back(value);
  }
  auto reply = impl_->command(args);
  return reply_string(reply.get());
}

std::vector<StreamEntry> RedisClient::xread(const std::string& stream, const std::string& after_id,
                                            std::chrono::milliseconds block, std::size_t count) {
  std::vector<std::string> args{"XREAD", "COUNT", std::to_string(count == 0 ? 1 : count)};
  if (block.count() > 0) {
    args.insert(args.end(), {"BLOCK", std::to_string(block.count())});
  }
  args.insert(args.end(), {"STREAMS", stream, after_id});
  auto reply = impl_->command(args, block.count() > 0 ? block : std::chrono::milliseconds{});
  return parse_read_reply(reply.get());
}

void RedisClient::xgroup_create(const std::string& stream, const std::string& group, const std::string& start_id) {
  try {
    impl_->command({"XGROUP", "CREATE", stream, group, start_id, "MKSTREAM"});
  } catch (const RedisError& e) {
    if (std::string_view(e.what()).find("BUSYGROUP") == std::string_view::npos) {
      throw;
    }
  }
}

std::vector<StreamEntry> RedisClient::xreadgroup(const std::string& stream, const std::string& group,
                                                 const std::string& consumer, std::chrono::milliseconds block,
                                                 std::size_t count, const std::string& id) {
  std::vector<std::string> args{"XREADGROUP", "GROUP", group, consumer, "COUNT", std::to_string(count == 0 ? 1 : count)};
  if (block.count() > 0) {
    args.insert(args.end(), {"BLOCK", std::to_string(block.count())});
  }
  args.insert(args.end(), {"STREAMS", stream, id});
  auto reply = impl_->command(args, block.count() > 0 ? block : std::chrono::milliseconds{});
  return parse_read_reply(reply.get());
}

void RedisClient::xack(const std::string& stream, const std::string& group, const std::string& id) {
  impl_->command({"XACK", stream, group, id});
}

RedisClient::AutoClaimed RedisClient::xautoclaim(const std::string& stream, const std::string& group,
                                                 const std::string& consumer, std::chrono::milliseconds min_idle,
                                                 const std::string& start_id, std::size_t count) {
  auto reply = impl_->command({"XAUTOCLAIM", stream, group, consumer, std::to_string(min_idle.count()), start_id,
                               "COUNT", std::to_string(count == 0 ? 1 : count)});
  if (reply->type != REDIS_REPLY_ARRAY || reply->elements < 2) {
    throw RedisError("redis: malformed XAUTOCLAIM reply");
  }
  AutoClaimed out;
  out.next_start_id = reply_string(reply->element[0]);
  const redisReply* entries = reply->element[1];
  if (entries != nullptr && entries->type == REDIS_REPLY_ARRAY) {
    for (std::size_t i = 0; i < entries->elements; ++i) {
      const redisReply* entry = entries->element[i];
      if (entry == nullptr || entry->type == REDIS_REPLY_NIL) {
        continue;  // Redis < 7 reports deleted entries as nil
      }
      out.entries.push_back(parse_entry(entry));
    }
  }
  return out;
}

std::vector<StreamEntry> RedisClient::xrange(const std::string& stream, const std::string& start,
                                             const std::string& end, std::size_t count) {
  std::vector<std::string> args{"XRANGE", stream, start, end};
  if (count > 0) {
    args.insert(args.end(), {"COUNT", std::to_string(count)});
  }
  auto reply = impl_->command(args);
  return parse_entries(reply.get());
}

long long RedisClient::xlen(const std::string& stream) {
  auto reply = impl_->command({"XLEN", stream});
  if (reply->type != REDIS_REPLY_INTEGER) {
    throw RedisError("redis: unexpected XLEN reply");
  }
  return reply->integer;
}

long long RedisClient::del(const std::string& key) {
  auto reply = impl_->command({"DEL", key});
  if (reply->type != REDIS_REPLY_INTEGER) {
    throw RedisError("redis: unexpected DEL reply");
  }
  return reply->integer;
}

bool RedisClient::ping() {
  try {
    auto reply = impl_->command({"PING"});
    return reply->type == REDIS_REPLY_STATUS && std::string_view(reply->str, reply->len) == "PONG";
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace sg::common
