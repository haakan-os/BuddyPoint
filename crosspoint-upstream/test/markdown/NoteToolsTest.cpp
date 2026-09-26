#include <HalStorage.h>
#include <NoteTools.h>
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <memory>

namespace {
void fixture(const char* name, const std::string& target) {
  std::ifstream input(std::string(MARKDOWN_FIXTURES_DIR) + "/" + name, std::ios::binary);
  mdtest::files[target] = std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}
class NoteToolsTest : public ::testing::Test {
 protected:
  const std::string source = "/Obsidian/tools.md";
  void SetUp() override {
    mdtest::reset();
    fixture("tools.md", source);
    fixture("tools.bnotes", notes::sidecar(source));
  }
};
}  // namespace
TEST_F(NoteToolsTest, PythonFixtureLinksAndCards) {
  auto tools = std::make_unique<notes::Tools>();
  ASSERT_TRUE(tools->load(source, 1));
  ASSERT_EQ(tools->count, 1u);
  EXPECT_STREQ(tools->labels[0], "Next note");
  std::string value;
  ASSERT_TRUE(tools->value(0, value));
  EXPECT_EQ(value, "/Obsidian/Next.md");
  ASSERT_TRUE(tools->load(source, 2));
  EXPECT_STREQ(tools->labels[0], "What is 2+2?");
  ASSERT_TRUE(tools->value(0, value));
  EXPECT_EQ(value, "Four");
  EXPECT_FALSE(tools->value(1, value));
}
TEST_F(NoteToolsTest, RejectsStaleCorruptAndTruncatedData) {
  auto tools = std::make_unique<notes::Tools>();
  mdtest::files[source][0] ^= 1;
  EXPECT_FALSE(tools->load(source, 1));
  EXPECT_EQ(tools->count, 0u);
  mdtest::files[source][0] ^= 1;
  auto& bytes = mdtest::files[notes::sidecar(source)];
  bytes.back() ^= 1;
  EXPECT_FALSE(tools->load(source, 1));
  bytes.pop_back();
  EXPECT_FALSE(tools->load(source, 2));
}
TEST_F(NoteToolsTest, FavouriteTogglePersistsAndKeepsPathsSeparate) {
  EXPECT_FALSE(notes::isFavourite(source));
  ASSERT_TRUE(notes::toggleFavourite(source));
  EXPECT_TRUE(notes::isFavourite(source));
  EXPECT_FALSE(notes::isFavourite("/Elsewhere/tools.md"));
  ASSERT_TRUE(notes::toggleFavourite(source));
  EXPECT_FALSE(notes::isFavourite(source));
}
TEST_F(NoteToolsTest, FailedFavouriteWriteIsNotPublished) {
  mdtest::failWriteSuffix = ".tmp";
  EXPECT_FALSE(notes::toggleFavourite(source));
  EXPECT_FALSE(notes::isFavourite(source));
  mdtest::failWriteSuffix.clear();
  mdtest::failClose = true;
  EXPECT_FALSE(notes::toggleFavourite(source));
  EXPECT_FALSE(notes::isFavourite(source));
}
