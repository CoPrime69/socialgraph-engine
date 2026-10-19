#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <zlib.h>

#include "sg/common/crypto.hpp"
#include "sg/common/csv.hpp"

namespace {

using namespace sg::common;
using Fields = std::vector<std::string>;

TEST(Csv, ParsesQuotedFieldsAndEscapes) {
  const auto records = parse_csv(
      "id,name,about\n"
      "1,\"Smith, John\",\"He said \"\"hi\"\"\"\n"
      "2,plain,\"two\nlines\"\n"
      "3,,\n");
  ASSERT_EQ(records.size(), 4U);
  EXPECT_EQ(records[0].fields, (Fields{"id", "name", "about"}));
  EXPECT_EQ(records[1].fields, (Fields{"1", "Smith, John", "He said \"hi\""}));
  EXPECT_EQ(records[2].fields, (Fields{"2", "plain", "two\nlines"}));
  EXPECT_EQ(records[3].fields, (Fields{"3", "", ""}));
  EXPECT_EQ(records[0].line, 1U);
  EXPECT_EQ(records[1].line, 2U);
  EXPECT_EQ(records[2].line, 3U);
  EXPECT_EQ(records[3].line, 5U);  // the quoted field spanned two lines
}

TEST(Csv, HandlesCrlfBomBlankLinesAndMissingFinalNewline) {
  const auto records = parse_csv("\xEF\xBB\xBF" "a,b\r\n\r\n1,\"x\"\r\n\n2,y");
  ASSERT_EQ(records.size(), 3U);
  EXPECT_EQ(records[0].fields, (Fields{"a", "b"}));
  EXPECT_EQ(records[1].fields, (Fields{"1", "x"}));
  EXPECT_EQ(records[2].fields, (Fields{"2", "y"}));
  EXPECT_EQ(records[2].line, 5U);
}

TEST(Csv, ReportsErrorsWithLineNumbers) {
  try {
    (void)parse_csv("a,b\n1,\"unterminated\n2,3\n");
    FAIL() << "expected CsvError";
  } catch (const CsvError& e) {
    EXPECT_EQ(e.line(), 2U);
    EXPECT_NE(std::string(e.what()).find("unterminated"), std::string::npos);
  }
  try {
    (void)parse_csv("a,b\n1,2\n3,x\"y\n");
    FAIL() << "expected CsvError";
  } catch (const CsvError& e) {
    EXPECT_EQ(e.line(), 3U);
  }
  EXPECT_THROW((void)parse_csv("\"a\"b,c\n"), CsvError);
}

class CsvFileTest : public ::testing::Test {
 protected:
  std::filesystem::path dir = std::filesystem::temp_directory_path() / ("sg_csv_" + random_hex(6));
  void SetUp() override { std::filesystem::create_directories(dir); }
  void TearDown() override { std::filesystem::remove_all(dir); }
};

TEST_F(CsvFileTest, ReaderStreamsPlainAndGzipFiles) {
  std::string content = "id,handle,about\n";
  for (int i = 1; i <= 5000; ++i) {
    content += std::to_string(i) + ",user" + std::to_string(i) + ",\"line one\nline, two\"\n";
  }
  const auto plain = dir / "users.csv";
  std::ofstream(plain, std::ios::binary) << content;
  const auto gz = dir / "users.csv.gz";
  gzFile out = gzopen(gz.c_str(), "wb");
  ASSERT_NE(out, nullptr);
  ASSERT_EQ(gzwrite(out, content.data(), static_cast<unsigned>(content.size())), static_cast<int>(content.size()));
  gzclose(out);

  for (const auto& path : {plain, gz}) {
    CsvReader reader(path);
    reader.expect_header({"id", "handle", "about"});
    std::size_t rows = 0;
    while (auto record = reader.next()) {
      ++rows;
      ASSERT_EQ(record->fields.size(), 3U);
      EXPECT_EQ(record->fields[0], std::to_string(rows));
      EXPECT_EQ(record->fields[2], "line one\nline, two");
      EXPECT_EQ(record->line, 2 * rows);  // every record spans two lines
    }
    EXPECT_EQ(rows, 5000U) << path;
  }
  EXPECT_EQ(read_text_file(gz), content);
}

TEST_F(CsvFileTest, ReaderReportsFileAndLine) {
  const auto path = dir / "bad.csv";
  std::ofstream(path) << "a,b\n1,2\n3,\"oops\n";
  CsvReader reader(path);
  reader.expect_header({"a", "b"});
  ASSERT_TRUE(reader.next());
  try {
    (void)reader.next();
    FAIL() << "expected CsvError";
  } catch (const CsvError& e) {
    EXPECT_EQ(std::string(e.what()), "bad.csv:3: unterminated quoted field");
  }
  CsvReader wrong_header(path);
  EXPECT_THROW(wrong_header.expect_header({"a", "c"}), CsvError);
  EXPECT_THROW(CsvReader(dir / "missing.csv"), std::runtime_error);
}

}  // namespace
