#include "dataset.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>

#ifndef SG_HAVE_ZLIB
#define SG_HAVE_ZLIB 0
#endif
#if SG_HAVE_ZLIB
#include <zlib.h>
#endif

#ifndef SG_DATASET_DIR
#define SG_DATASET_DIR "data/stackoverflow"
#endif

namespace sg::bench {

namespace {

// ---- byte sources (RAII) ----------------------------------------------------------------------

class ByteSource {
 public:
  ByteSource() = default;
  ByteSource(const ByteSource&) = delete;
  ByteSource& operator=(const ByteSource&) = delete;
  virtual ~ByteSource() = default;
  /// Reads up to `capacity` bytes; 0 at end of input. Throws on I/O errors.
  virtual std::size_t read(char* buffer, std::size_t capacity) = 0;
};

#if SG_HAVE_ZLIB
class GzipSource final : public ByteSource {
 public:
  explicit GzipSource(gzFile file) : file_(file) {}
  ~GzipSource() override { gzclose(file_); }
  std::size_t read(char* buffer, std::size_t capacity) override {
    const int n = gzread(file_, buffer, static_cast<unsigned>(capacity));
    if (n < 0) {
      int code = 0;
      throw std::runtime_error(std::string("gzread failed: ") + gzerror(file_, &code));
    }
    return static_cast<std::size_t>(n);
  }

 private:
  gzFile file_;
};
#endif

class FileSource final : public ByteSource {
 public:
  explicit FileSource(std::FILE* file) : file_(file) {}
  ~FileSource() override { std::fclose(file_); }
  std::size_t read(char* buffer, std::size_t capacity) override {
    const std::size_t n = std::fread(buffer, 1, capacity, file_);
    if (n == 0 && std::ferror(file_) != 0) {
      throw std::runtime_error("fread failed");
    }
    return n;
  }

 private:
  std::FILE* file_;
};

/// Opens `<base>.gz` through zlib when available, else the plain `<base>`.
std::unique_ptr<ByteSource> open_source(const std::string& base) {
#if SG_HAVE_ZLIB
  if (std::FILE* probe = std::fopen((base + ".gz").c_str(), "rb")) {
    std::fclose(probe);
    if (gzFile gz = gzopen((base + ".gz").c_str(), "rb")) {
      gzbuffer(gz, 1U << 18U);
      return std::make_unique<GzipSource>(gz);
    }
  }
#endif
  if (std::FILE* plain = std::fopen(base.c_str(), "rb")) {
    return std::make_unique<FileSource>(plain);
  }
  return nullptr;
}

// ---- streaming RFC 4180 reader ----------------------------------------------------------------

class CsvReader {
 public:
  explicit CsvReader(std::unique_ptr<ByteSource> source) : source_(std::move(source)), buffer_(1U << 20U) {}

  /// Next record into `fields`; false at end of input. Handles quoted fields with embedded
  /// commas, doubled quotes and line breaks, and both LF and CRLF line endings.
  bool next(std::vector<std::string>& fields) {
    fields.clear();
    int c = get();
    if (c < 0) {
      return false;
    }
    std::string field;
    bool quoted = false;
    for (;;) {
      if (quoted) {
        if (c < 0) {
          throw std::runtime_error("CSV: unterminated quoted field");
        }
        if (c == '"') {
          const int d = get();
          if (d == '"') {
            field += '"';
          } else {
            quoted = false;
            c = d;
            continue;
          }
        } else {
          field += static_cast<char>(c);
        }
      } else if (c < 0 || c == '\n') {
        fields.push_back(std::move(field));
        return true;
      } else if (c == '\r') {
        const int d = get();
        if (d < 0 || d == '\n') {
          fields.push_back(std::move(field));
          return true;
        }
        field += '\r';
        c = d;
        continue;
      } else if (c == ',') {
        fields.push_back(std::move(field));
        field.clear();
      } else if (c == '"' && field.empty()) {
        quoted = true;
      } else {
        field += static_cast<char>(c);
      }
      c = get();
    }
  }

 private:
  int get() {
    if (position_ == length_) {
      length_ = source_->read(buffer_.data(), buffer_.size());
      position_ = 0;
      if (length_ == 0) {
        return -1;
      }
    }
    return static_cast<unsigned char>(buffer_[position_++]);
  }

  std::unique_ptr<ByteSource> source_;
  std::vector<char> buffer_;
  std::size_t position_ = 0;
  std::size_t length_ = 0;
};

/// Column positions of a header, by name.
class Header {
 public:
  explicit Header(const std::vector<std::string>& names) : names_(names) {}
  [[nodiscard]] std::size_t at(const std::string& name) const {
    for (std::size_t i = 0; i < names_.size(); ++i) {
      if (names_[i] == name) {
        return i;
      }
    }
    throw std::runtime_error("CSV: missing column '" + name + "'");
  }

 private:
  std::vector<std::string> names_;
};

template <class T>
T parse_number(const std::string& text, const char* what) {
  T value{};
  const auto* first = text.data();
  const auto* last = text.data() + text.size();
  const auto [end, ec] = std::from_chars(first, last, value);
  if (ec != std::errc{} || end != last) {
    throw std::runtime_error(std::string("CSV: bad ") + what + " '" + text + "'");
  }
  return value;
}

/// Name → code in file order (the display order of contracts.md §10.3), from a CSV column.
std::unordered_map<std::string, std::uint16_t> read_codes(const std::string& base, const char* column) {
  std::unordered_map<std::string, std::uint16_t> codes;
  auto source = open_source(base);
  if (!source) {
    return codes;
  }
  CsvReader reader(std::move(source));
  std::vector<std::string> row;
  if (!reader.next(row)) {
    return codes;
  }
  const std::size_t col = Header(row).at(column);
  while (reader.next(row)) {
    if (row.size() > col && !row[col].empty()) {
      codes.try_emplace(row[col], static_cast<std::uint16_t>(codes.size()));
    }
  }
  return codes;
}

std::uint16_t code_of(std::unordered_map<std::string, std::uint16_t>& codes, const std::string& name) {
  // Values missing from the taxonomy files get the next free code (first appearance order).
  return codes.try_emplace(name, static_cast<std::uint16_t>(codes.size())).first->second;
}

}  // namespace

std::string dataset_dir() {
  if (const char* env = std::getenv("SG_DATASET_DIR"); env != nullptr && *env != '\0') {
    return env;
  }
  return SG_DATASET_DIR;
}

bool has_zlib() { return SG_HAVE_ZLIB != 0; }

std::optional<BenchGraph> load_stackoverflow(const std::string& dir, std::string& error) {
  try {
    auto user_source = open_source(dir + "/users.csv");
    auto connection_source = open_source(dir + "/connections.csv");
    if (!user_source || !connection_source) {
      error = "users.csv(.gz) / connections.csv(.gz) not found in " + dir +
              (has_zlib() ? "" : " (built without zlib: only plain .csv files can be read)");
      return std::nullopt;
    }
    auto category_codes = read_codes(dir + "/categories.csv", "name");
    auto branch_codes = read_codes(dir + "/branches.csv", "name");

    BenchGraph out;
    out.name = "stackoverflow";
    std::unordered_map<std::uint64_t, graph::NodeId> node_of;
    {
      CsvReader reader(std::move(user_source));
      std::vector<std::string> row;
      if (!reader.next(row)) {
        error = "users.csv is empty";
        return std::nullopt;
      }
      const Header header(row);
      const std::size_t c_id = header.at("id");
      const std::size_t c_influence = header.at("influence");
      const std::size_t c_category = header.at("category");
      const std::size_t c_branch = header.at("branch");
      while (reader.next(row)) {
        if (row.size() == 1 && row[0].empty()) {
          continue;
        }
        const auto id = parse_number<std::uint64_t>(row.at(c_id), "user id");
        const auto node = static_cast<graph::NodeId>(out.profiles.size());
        if (!node_of.try_emplace(id, node).second) {
          throw std::runtime_error("users.csv: duplicate id " + row[c_id]);
        }
        const double influence = row.at(c_influence).empty() ? 0.0 : parse_number<double>(row[c_influence], "influence");
        out.profiles.push_back(graph::Profile{code_of(category_codes, row.at(c_category)),
                                              code_of(branch_codes, row.at(c_branch)), influence});
        out.external_ids.push_back(row[c_id]);
        out.categories.push_back(row[c_category]);
        out.branches.push_back(row[c_branch]);
      }
    }
    {
      CsvReader reader(std::move(connection_source));
      std::vector<std::string> row;
      if (!reader.next(row)) {
        error = "connections.csv is empty";
        return std::nullopt;
      }
      const Header header(row);
      const std::size_t c_a = header.at("user_a");
      const std::size_t c_b = header.at("user_b");
      const std::size_t c_strength = header.at("strength");
      while (reader.next(row)) {
        if (row.size() == 1 && row[0].empty()) {
          continue;
        }
        const auto a = node_of.find(parse_number<std::uint64_t>(row.at(c_a), "user_a"));
        const auto b = node_of.find(parse_number<std::uint64_t>(row.at(c_b), "user_b"));
        if (a == node_of.end() || b == node_of.end()) {
          throw std::runtime_error("connections.csv: unknown user in row " + row[c_a] + "," + row[c_b]);
        }
        out.edges.push_back(graph::WeightedEdge{a->second, b->second, parse_number<double>(row.at(c_strength), "strength")});
      }
    }
    out.graph = graph::CsrGraph::from_edges(out.profiles.size(), out.edges);
    return out;
  } catch (const std::exception& e) {
    error = e.what();
    return std::nullopt;
  }
}

BenchGraph make_synthetic(std::size_t n) {
  BenchGraph out;
  out.name = "synthetic-" + std::to_string(n);
  const std::size_t group_count = std::max<std::size_t>(1, n / 32);
  graph::SplitMix64 rng(n);
  std::vector<std::uint32_t> groups(n);
  for (auto& g : groups) {
    g = static_cast<std::uint32_t>(rng.bounded(group_count));
  }
  out.profiles.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    const auto branch = static_cast<std::uint16_t>(groups[i] % 73U);  // like the real taxonomy
    const auto category = static_cast<std::uint16_t>(branch % 10U);
    out.profiles[i] = graph::Profile{category, branch, static_cast<double>(rng.bounded(10001)) / 100.0};
    out.external_ids.push_back("user" + std::to_string(i + 1));
    out.categories.push_back("category" + std::to_string(category));
    out.branches.push_back("branch" + std::to_string(branch));
  }
  graph::GeneratorOptions options;
  options.average_degree = 10.0;
  options.mixing = 0.2;
  for (const auto& [u, v] : graph::generate_homophilous_edges(groups, options)) {
    out.edges.push_back(graph::WeightedEdge{u, v, graph::connection_strength(out.profiles[u], out.profiles[v])});
  }
  out.graph = graph::CsrGraph::from_edges(n, out.edges);
  return out;
}

}  // namespace sg::bench
