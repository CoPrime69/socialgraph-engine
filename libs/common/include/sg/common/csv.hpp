#pragma once

/// @file csv.hpp
/// RFC 4180 CSV parsing (quoted fields, escaped quotes, embedded separators and line breaks),
/// from memory or streamed from a plain or gzip-compressed file.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sg::common {

/// A parse error; what() is "<source>:<line>: <message>" (or "line <line>: <message>").
class CsvError : public std::runtime_error {
 public:
  CsvError(std::size_t line, const std::string& message, const std::string& source = {});
  [[nodiscard]] std::size_t line() const noexcept { return line_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }

 private:
  std::size_t line_;
  std::string message_;
};

struct CsvRecord {
  std::size_t line = 0;  ///< 1-based line on which the record starts
  std::vector<std::string> fields;
};

/// Parses `text` into records. Accepts LF and CRLF line ends, strips a UTF-8 byte order mark and
/// skips blank lines. @throws CsvError on an unterminated quoted field, a quote inside an unquoted
/// field or text after a closing quote.
[[nodiscard]] std::vector<CsvRecord> parse_csv(std::string_view text);

/// Streams records from a file; files ending in ".gz" (or starting with the gzip magic bytes) are
/// decompressed on the fly. Same dialect as parse_csv(). Not thread-safe.
class CsvReader {
 public:
  /// @throws std::runtime_error naming the path when the file cannot be opened.
  explicit CsvReader(const std::filesystem::path& path);
  ~CsvReader();
  CsvReader(const CsvReader&) = delete;
  CsvReader& operator=(const CsvReader&) = delete;

  /// Next record, or std::nullopt at end of file. @throws CsvError (message prefixed with the
  /// file name) or std::runtime_error on read/decompression errors.
  [[nodiscard]] std::optional<CsvRecord> next();

  /// Reads the header record and checks it equals `expected` (exact, in order).
  /// @throws CsvError when it differs or the file is empty.
  void expect_header(const std::vector<std::string_view>& expected);

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  struct Impl;
  std::filesystem::path path_;
  std::unique_ptr<Impl> impl_;
};

/// Reads a whole file (gzip-compressed files are decompressed).
/// @throws std::runtime_error naming the path when it cannot be read.
[[nodiscard]] std::string read_text_file(const std::filesystem::path& path);

}  // namespace sg::common
