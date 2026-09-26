#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

#include "MarkdownParser.h"
#include "StoredZipWriter.h"

namespace {
bool collect(void* context, const char* bytes, size_t size) {
  static_cast<std::string*>(context)->append(bytes, size);
  return true;
}
std::string render(const std::string& source, size_t chunk = 17) {
  std::string output;
  markdown::Parser parser(collect, &output);
  for (size_t i = 0; i < source.size(); i += chunk)
    EXPECT_TRUE(parser.feed(source.data() + i, std::min(chunk, source.size() - i)));
  EXPECT_TRUE(parser.finish());
  return output;
}
struct ZipBuffer {
  std::vector<uint8_t> bytes;
  size_t pos = 0;
  static bool write(void* ctx, const uint8_t* data, size_t size) {
    auto& self = *static_cast<ZipBuffer*>(ctx);
    self.bytes.resize(std::max(self.bytes.size(), self.pos + size));
    std::copy_n(data, size, self.bytes.data() + self.pos);
    self.pos += size;
    return true;
  }
  static bool seek(void* ctx, uint32_t pos) {
    auto& self = *static_cast<ZipBuffer*>(ctx);
    if (pos > self.bytes.size()) return false;
    self.pos = pos;
    return true;
  }
  uint32_t u32(size_t pos) const {
    return bytes[pos] | uint32_t(bytes[pos + 1]) << 8 | uint32_t(bytes[pos + 2]) << 16 | uint32_t(bytes[pos + 3]) << 24;
  }
};
}  // namespace

TEST(Markdown, HeadingsAndContentsCallbacks) {
  std::string html;
  std::vector<std::string> headings;
  markdown::Parser parser(
      collect, &html,
      [](void* ctx, unsigned id, std::string_view label) {
        auto& headings = *static_cast<std::vector<std::string>*>(ctx);
        EXPECT_EQ(id, headings.size() + 1);
        headings.emplace_back(label);
        return true;
      },
      &headings);
  const std::string input = "# Title\n\nSubtitle\n===\n\nSection\n---\n\n### Three ###\n";
  ASSERT_TRUE(parser.feed(input.data(), input.size()));
  ASSERT_TRUE(parser.finish());
  EXPECT_EQ(headings, (std::vector<std::string>{"Title", "Subtitle", "Section", "Three"}));
  EXPECT_NE(html.find("<h1 id=\"heading-1\">Title</h1>"), std::string::npos);
  EXPECT_NE(html.find("<h2 id=\"heading-3\">Section</h2>"), std::string::npos);
}
TEST(Markdown, EmphasisCodeEscapesAndOfflineLinks) {
  const auto html = render(
      "**bold** and *italic* plus `a < b` and \\*literal\\*\n[Site](https://example.com/a_(b)) ![Alt](img.png)\nraw "
      "<script>&\n");
  EXPECT_NE(html.find("<strong>bold</strong>"), std::string::npos);
  EXPECT_NE(html.find("<em>italic</em>"), std::string::npos);
  EXPECT_NE(html.find("<code>a &lt; b</code>"), std::string::npos);
  EXPECT_NE(html.find("*literal*"), std::string::npos);
  EXPECT_NE(html.find("Site [Alt]"), std::string::npos);
  EXPECT_NE(html.find("&lt;script&gt;&amp;"), std::string::npos);
  EXPECT_EQ(html.find("https://"), std::string::npos);
  EXPECT_EQ(html.find("<script>"), std::string::npos);
}
TEST(Markdown, ListsQuotesAndHorizontalRules) {
  const auto html = render("- one\n- [x] done\n\n1. first\n2. second\n\n> a quote\n> next\n\n---\n");
  EXPECT_NE(html.find("<ul><li>one</li>\n<li>[x] done</li>"), std::string::npos);
  EXPECT_NE(html.find("<ol><li>first</li>\n<li>second</li>"), std::string::npos);
  EXPECT_NE(html.find("<blockquote><p>a quote</p>\n<p>next</p>"), std::string::npos);
  EXPECT_NE(html.find("<hr/>"), std::string::npos);
}
TEST(Markdown, TablesUseHeaderAndDataCells) {
  const auto html = render("| Name | Value |\n| --- | :---: |\n| **A** | 1 |\n| B | 2 |\n\nAfter\n");
  EXPECT_NE(html.find("<table><thead><tr><th>Name</th><th>Value</th></tr>"), std::string::npos);
  EXPECT_NE(html.find("<td><strong>A</strong></td><td>1</td>"), std::string::npos);
  EXPECT_NE(html.find("</tbody></table>\n<p>After"), std::string::npos);
}
TEST(Markdown, FencesKeepLiteralCodeAndCloseAtEndOfFile) {
  EXPECT_EQ(render("```cpp\n# not a heading\n**literal** & <tag>\n```\n"),
            "<div><pre><code>#&#160;not&#160;a&#160;heading<br/>\n**literal**&#160;&amp;&#160;&lt;tag&gt;<br/>\n</"
            "code></pre></div>\n");
  EXPECT_EQ(render("~~~\n  indented\n"), "<div><pre><code>&#160;&#160;indented<br/>\n</code></pre></div>\n");
  EXPECT_EQ(render("    code\n    next\n"), "<div><pre><code>code<br/>\nnext<br/>\n</code></pre></div>\n");
}
TEST(Markdown, HandlesBomCrLfUtf8AndMissingFinalNewline) {
  const std::string source = "\xef\xbb\xbf# Héllo 世界\r\n\r\nText é 😀";
  EXPECT_EQ(render(source, 1), render(source, 4096));
  EXPECT_NE(render(source).find("Héllo 世界</h1>"), std::string::npos);
  EXPECT_NE(render(source).find("Text é 😀"), std::string::npos);
  EXPECT_EQ(render(source).find('\r'), std::string::npos);
}
TEST(Markdown, LongLinesNeverDropOrSplitUnicodeCharacters) {
  const std::string source = std::string(2047, 'a') + "😀" + std::string(5000, 'b');
  auto html = render(source, 1);
  EXPECT_NE(html.find(source), std::string::npos);
  EXPECT_EQ(html, render(source, 4096));
}
TEST(Markdown, EmptyMalformedAndUnmatchedMarkupStaysReadable) {
  EXPECT_TRUE(render("").empty());
  const auto html = render("Unmatched **bold and [link](missing\nword_with_underscores\n");
  EXPECT_NE(html.find("**bold"), std::string::npos);
  EXPECT_NE(html.find("[link](missing"), std::string::npos);
  EXPECT_NE(html.find("word_with_underscores"), std::string::npos);
}
TEST(Markdown, OutputFailuresPropagate) {
  markdown::Parser parser([](void*, const char*, size_t) { return false; }, nullptr);
  const std::string text = "# Header\nBody\n";
  EXPECT_FALSE(parser.feed(text.data(), text.size()));
  EXPECT_FALSE(parser.finish());
}
TEST(MarkdownZip, WritesStoredMembersWithCrcAndCentralDirectory) {
  ZipBuffer out;
  markdown::StoredZipWriter zip(ZipBuffer::write, ZipBuffer::seek, &out);
  ASSERT_TRUE(zip.begin("mimetype"));
  ASSERT_TRUE(zip.append("123456789", 9));
  ASSERT_TRUE(zip.end());
  ASSERT_TRUE(zip.begin("content.xhtml"));
  ASSERT_TRUE(zip.append("hello", 5));
  ASSERT_TRUE(zip.end());
  ASSERT_TRUE(zip.finish());
  EXPECT_EQ(out.u32(0), 0x04034b50u);
  EXPECT_EQ(out.u32(14), 0xcbf43926u);  // Standard CRC32 test vector.
  EXPECT_EQ(out.u32(18), 9u);
  EXPECT_EQ(out.u32(22), 9u);
  const size_t eocd = out.bytes.size() - 22;
  EXPECT_EQ(out.u32(eocd), 0x06054b50u);
  EXPECT_EQ(out.u32(out.u32(eocd + 16)), 0x02014b50u);
  EXPECT_EQ(out.bytes[eocd + 8], 2);
}
TEST(MarkdownZip, RejectsInvalidUsageAndWriteFailure) {
  ZipBuffer out;
  markdown::StoredZipWriter bad(ZipBuffer::write, ZipBuffer::seek, &out);
  EXPECT_FALSE(bad.append("x", 1));
  markdown::StoredZipWriter nested(ZipBuffer::write, ZipBuffer::seek, &out);
  EXPECT_TRUE(nested.begin("one"));
  EXPECT_FALSE(nested.begin("two"));
  markdown::StoredZipWriter failed([](void*, const uint8_t*, size_t) { return false; }, ZipBuffer::seek, &out);
  EXPECT_FALSE(failed.begin("one"));
  EXPECT_FALSE(failed.finish());
}

TEST(Markdown, FencesAndListItemsCannotBecomeSetextHeadings) {
  const auto html = render("```\n---\ncode\n```\n\n- item\n---\n");
  EXPECT_EQ(html.find("<h"), html.find("<hr/>"));
  EXPECT_NE(html.find("---<br/>"), std::string::npos);
  EXPECT_NE(html.find("<li>item</li>"), std::string::npos);
}
TEST(Markdown, LongCodeLinesKeepTheirFollowingLineBreak) {
  const std::string line(7000, 'a');
  const auto html = render("```\n" + line + "\nnext\n```\n");
  EXPECT_NE(html.find(line + "<br/>\nnext<br/>"), std::string::npos);
}
TEST(Markdown, NestedEmphasisProducesBalancedTags) {
  EXPECT_NE(render("***both***").find("<strong><em>both</em></strong>"), std::string::npos);
  EXPECT_NE(render("**bold and *italic***").find("<strong>bold and <em>italic</em></strong>"), std::string::npos);
}
