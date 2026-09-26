#include <HalStorage.h>
#include <expat.h>
#include <gtest/gtest.h>
#include <zlib.h>

#include "MarkdownDocument.h"

namespace {
class MarkdownDocumentTest : public testing::Test {
 protected:
  void SetUp() override { mdtest::reset(); }
  void source(const std::string& text) { mdtest::files["/Notes.md"] = std::vector<uint8_t>(text.begin(), text.end()); }
  static uint32_t u32(const std::vector<uint8_t>& bytes, size_t at) {
    return bytes.at(at) | uint32_t(bytes.at(at + 1)) << 8 | uint32_t(bytes.at(at + 2)) << 16 |
           uint32_t(bytes.at(at + 3)) << 24;
  }
  static uint16_t u16(const std::vector<uint8_t>& bytes, size_t at) {
    return bytes.at(at) | uint16_t(bytes.at(at + 1)) << 8;
  }
  std::map<std::string, std::string> members(const std::string& path) {
    const auto& bytes = mdtest::files.at(path);
    std::map<std::string, std::string> result;
    size_t pos = 0;
    while (u32(bytes, pos) == 0x04034b50u) {
      const size_t size = u32(bytes, pos + 18), nameSize = u16(bytes, pos + 26), extraSize = u16(bytes, pos + 28);
      EXPECT_EQ(u16(bytes, pos + 8), 0);  // EPUB members are stored, not compressed.
      const std::string name(reinterpret_cast<const char*>(bytes.data() + pos + 30), nameSize);
      const size_t data = pos + 30 + nameSize + extraSize;
      EXPECT_LE(data + size, bytes.size());
      EXPECT_EQ(crc32(0, bytes.data() + data, size), u32(bytes, pos + 14));
      result[name] = std::string(reinterpret_cast<const char*>(bytes.data() + data), size);
      pos = data + size;
    }
    EXPECT_EQ(u32(bytes, pos), 0x02014b50u);
    EXPECT_EQ(u32(bytes, bytes.size() - 22), 0x06054b50u);
    return result;
  }
};
}  // namespace

TEST_F(MarkdownDocumentTest, BuildsWellFormedEpubWithHeadingsAndKeepsSource) {
  const std::string text =
      "# Notes & tasks\n\n**Bold** and *italic*.\n\n## Code\n```\n  if (a < b)\n    return;\n```\n\n| A | B |\n| --- | "
      "--- |\n| 1 | 2 |\n";
  source(text);
  std::string path;
  ASSERT_TRUE(markdown::prepareDocument("/Notes.md", path));
  const auto files = members(path);
  ASSERT_EQ(files.size(), 5u);
  EXPECT_EQ(files.at("mimetype"), "application/epub+zip");
  EXPECT_NE(files.at("toc.ncx").find("content.xhtml#heading-2"), std::string::npos);
  EXPECT_NE(files.at("content.xhtml").find("<strong>Bold</strong>"), std::string::npos);
  for (const auto& [name, contents] : files) {
    if (name == "mimetype") continue;
    auto parser = XML_ParserCreate(nullptr);
    ASSERT_NE(parser, nullptr);
    EXPECT_EQ(XML_Parse(parser, contents.data(), static_cast<int>(contents.size()), true), XML_STATUS_OK)
        << name << ": " << XML_ErrorString(XML_GetErrorCode(parser));
    XML_ParserFree(parser);
  }
  EXPECT_EQ(std::string(mdtest::files["/Notes.md"].begin(), mdtest::files["/Notes.md"].end()), text);
}
TEST_F(MarkdownDocumentTest, WarmOpenDoesNotRewriteOrDiscardProgress) {
  source("# Original\n");
  std::string path;
  ASSERT_TRUE(markdown::prepareDocument("/Notes.md", path));
  const std::string cache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}("/Notes.md"));
  mdtest::directories.insert(cache);
  mdtest::files[cache + "/progress.bin"] = {1, 2, 3};
  const size_t before = mdtest::writes;
  ASSERT_TRUE(markdown::prepareDocument("/Notes.md", path));
  EXPECT_EQ(mdtest::writes, before);
  EXPECT_TRUE(mdtest::files.contains(cache + "/progress.bin"));
}
TEST_F(MarkdownDocumentTest, SameSizeEditRebuildsAndInvalidatesOldPagination) {
  source("# AAAA\n");
  std::string path;
  ASSERT_TRUE(markdown::prepareDocument("/Notes.md", path));
  const auto previous = mdtest::files[path];
  const std::string cache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}("/Notes.md"));
  mdtest::directories.insert(cache);
  mdtest::files[cache + "/book.bin"] = {1};
  source("# BBBB\n");
  ASSERT_TRUE(markdown::prepareDocument("/Notes.md", path));
  EXPECT_NE(mdtest::files[path], previous);
  EXPECT_FALSE(mdtest::files.contains(cache + "/book.bin"));
  EXPECT_NE(members(path).at("content.xhtml").find("BBBB"), std::string::npos);
}
TEST_F(MarkdownDocumentTest, FailedConversionPreservesPreviousArchiveAndCleansTemporaryFiles) {
  source("Old text\n");
  std::string path;
  ASSERT_TRUE(markdown::prepareDocument("/Notes.md", path));
  const auto original = mdtest::files[path];
  source("New text\n");
  mdtest::failWriteSuffix = ".epub.tmp";
  EXPECT_FALSE(markdown::prepareDocument("/Notes.md", path));
  EXPECT_EQ(mdtest::files[path], original);
  for (const auto& [name, unused] : mdtest::files) EXPECT_FALSE(name.ends_with(".tmp"));
  mdtest::failWriteSuffix.clear();
  ASSERT_TRUE(markdown::prepareDocument("/Notes.md", path));
  EXPECT_NE(members(path).at("content.xhtml").find("New text"), std::string::npos);
}
TEST_F(MarkdownDocumentTest, ShortHtmlWriteAndReadErrorsFailWithoutPublishing) {
  source("Hello\n");
  std::string path;
  mdtest::failWriteSuffix = ".html.tmp";
  EXPECT_FALSE(markdown::prepareDocument("/Notes.md", path));
  EXPECT_FALSE(mdtest::files.contains(path));
  mdtest::failWriteSuffix.clear();
  mdtest::failRead = true;
  EXPECT_FALSE(markdown::prepareDocument("/Notes.md", path));
}
TEST_F(MarkdownDocumentTest, MissingOrInvalidMarkerRebuildsAnInterruptedPublication) {
  source("Hello\n");
  std::string path;
  ASSERT_TRUE(markdown::prepareDocument("/Notes.md", path));
  const std::string marker = path.substr(0, path.size() - 5) + ".meta";
  mdtest::files[marker] = {0};
  const size_t before = mdtest::writes;
  ASSERT_TRUE(markdown::prepareDocument("/Notes.md", path));
  EXPECT_GT(mdtest::writes, before);
  EXPECT_EQ(mdtest::files[marker].size(), 16u);
}
