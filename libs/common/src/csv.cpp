#include "sg/common/csv.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

#include <zlib.h>

namespace sg::common {
namespace {

constexpr int kEof = -1;

/// Characters from an in-memory buffer.
class StringSource {
 public:
  explicit StringSource(std::string_view text) : text_(text) {}
  int get() { return pos_ < text_.size() ? static_cast<unsigned char>(text_[pos_++]) : kEof; }
  int peek() const { return pos_ < text_.size() ? static_cast<unsigned char>(text_[pos_]) : kEof; }

 private:
  std::string_view text_;
  std::size_t pos_ = 0;
};

/// Characters from a (possibly gzip-compressed) file via zlib, which reads plain files unchanged.
class GzSource {
 public:
  explicit GzSource(const std::filesystem::path& path) : path_(path) {
    file_ = gzopen(path.c_str(), "rb");
    if (file_ == nullptr) {
      throw std::runtime_error("cannot open " + path.string() + ": " + std::strerror(errno));
    }
    gzbuffer(file_, 1U << 17U);
  }
  ~GzSource() {
    if (file_ != nullptr) {
      gzclose(file_);
    }
  }
  GzSource(const GzSource&) = delete;
  GzSource& operator=(const GzSource&) = delete;

  int get() {
    if (pos_ == len_ && !fill()) {
      return kEof;
    }
    return static_cast<unsigned char>(buffer_[pos_++]);
  }
  int peek() {
    if (pos_ == len_ && !fill()) {
      return kEof;
    }
    return static_cast<unsigned char>(buffer_[pos_]);
  }

 private:
  bool fill() {
    const int n = gzread(file_, buffer_.data(), static_cast<unsigned>(buffer_.size()));
    if (n < 0) {
      int code = 0;
      const char* message = gzerror(file_, &code);
      throw std::runtime_error("cannot read " + path_.string() + ": " + (message != nullptr ? message : "error"));
    }
    pos_ = 0;
    len_ = static_cast<std::size_t>(n);
    return len_ > 0;
  }

  std::filesystem::path path_;
  gzFile file_ = nullptr;
  std::array<char, 1U << 16U> buffer_{};
  std::size_t pos_ = 0;
  std::size_t len_ = 0;
};

/// RFC 4180 record parser over any source with get()/peek().
template <class Source>
class Parser {
 public:
  explicit Parser(Source& source) : source_(source) {}

  /// Parses the next non-blank record into `out`; false at end of input.
  bool next(CsvRecord& out) {
    if (!started_) {
      started_ = true;
      skip_bom();
    }
    for (;;) {
      if (source_.peek() == kEof) {
        return false;
      }
      out.line = line_;
      out.fields.clear();
      read_record(out.fields);
      const bool blank = out.fields.size() == 1 && out.fields.front().empty();
      if (!blank) {
        return true;
      }
    }
  }

 private:
  void skip_bom() {
    if (source_.peek() == 0xEF) {
      source_.get();
      if (source_.get() != 0xBB || source_.get() != 0xBF) {
        throw CsvError(line_, "invalid byte order mark");
      }
    }
  }

  /// Consumes one record including its line terminator.
  void read_record(std::vector<std::string>& fields) {
    for (;;) {
      std::string field;
      const int terminator = read_field(field);
      fields.push_back(std::move(field));
      if (terminator != ',') {
        return;  // end of line or input
      }
    }
  }

  /// Reads one field; returns ',' when another field follows, '\n' at end of line, kEof at end.
  int read_field(std::string& field) {
    if (source_.peek() == '"') {
      source_.get();
      const std::size_t start_line = line_;
      for (;;) {
        const int c = source_.get();
        if (c == kEof) {
          throw CsvError(start_line, "unterminated quoted field");
        }
        if (c == '"') {
          if (source_.peek() == '"') {
            source_.get();
            field.push_back('"');
            continue;
          }
          break;
        }
        if (c == '\n') {
          ++line_;
        }
        field.push_back(static_cast<char>(c));
      }
      const int after = source_.get();
      if (after == ',' || after == kEof) {
        return after;
      }
      if (after == '\n') {
        ++line_;
        return '\n';
      }
      if (after == '\r' && source_.peek() == '\n') {
        source_.get();
        ++line_;
        return '\n';
      }
      throw CsvError(line_, "unexpected character after closing quote");
    }
    for (;;) {
      const int c = source_.get();
      if (c == ',' || c == kEof) {
        return c;
      }
      if (c == '\n') {
        ++line_;
        return '\n';
      }
      if (c == '\r' && source_.peek() == '\n') {
        source_.get();
        ++line_;
        return '\n';
      }
      if (c == '"') {
        throw CsvError(line_, "quote inside an unquoted field");
      }
      field.push_back(static_cast<char>(c));
    }
  }

  Source& source_;
  std::size_t line_ = 1;
  bool started_ = false;
};

}  // namespace

CsvError::CsvError(std::size_t line, const std::string& message, const std::string& source)
    : std::runtime_error((source.empty() ? "line " : source + ":") + std::to_string(line) + ": " + message),
      line_(line),
      message_(message) {}

std::vector<CsvRecord> parse_csv(std::string_view text) {
  StringSource source(text);
  Parser<StringSource> parser(source);
  std::vector<CsvRecord> records;
  CsvRecord record;
  while (parser.next(record)) {
    records.push_back(std::move(record));
    record = CsvRecord{};
  }
  return records;
}

struct CsvReader::Impl {
  explicit Impl(const std::filesystem::path& path) : source(path), parser(source) {}
  GzSource source;
  Parser<GzSource> parser;
};

CsvReader::CsvReader(const std::filesystem::path& path) : path_(path), impl_(std::make_unique<Impl>(path)) {}

CsvReader::~CsvReader() = default;

std::optional<CsvRecord> CsvReader::next() {
  CsvRecord record;
  try {
    if (!impl_->parser.next(record)) {
      return std::nullopt;
    }
  } catch (const CsvError& e) {
    throw CsvError(e.line(), e.message(), path_.filename().string());
  }
  return record;
}

void CsvReader::expect_header(const std::vector<std::string_view>& expected) {
  const auto header = next();
  if (!header) {
    throw CsvError(1, "file is empty", path_.filename().string());
  }
  bool same = header->fields.size() == expected.size();
  for (std::size_t i = 0; same && i < expected.size(); ++i) {
    same = header->fields[i] == expected[i];
  }
  if (!same) {
    std::string wanted;
    for (const auto& column : expected) {
      wanted += (wanted.empty() ? "" : ",") + std::string(column);
    }
    throw CsvError(header->line, "expected header '" + wanted + "'", path_.filename().string());
  }
}

std::string read_text_file(const std::filesystem::path& path) {
  GzSource source(path);
  std::string out;
  for (int c = source.get(); c != kEof; c = source.get()) {
    out.push_back(static_cast<char>(c));
  }
  return out;
}

}  // namespace sg::common
