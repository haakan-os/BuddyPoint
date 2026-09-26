#include <AtomicFile.h>
#include <HalStorage.h>
#include <MarkdownChecklist.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {
void put(const std::string& path, const std::string& text) { mdtest::files[path] = {text.begin(), text.end()}; }
std::string get(const std::string& path) {
  const auto& b = mdtest::files.at(path);
  return {b.begin(), b.end()};
}
class MarkdownChecklistTest : public ::testing::Test {
 protected:
  void SetUp() override { mdtest::reset(); }
};
}  // namespace
TEST_F(MarkdownChecklistTest, ParsesTasksButSkipsCodeAndQuotes) {
  put("/todo.md",
      "\xef\xbb\xbf# Tasks\r\n- [ ] Milk\r\n  * [X] Café\n2. [x] Done\n> - [ ] Quote\n"
      "```md\n- [ ] Code\n```\n~~~\n- [ ] Code too\n~~~\n    - [ ] Indented code\n- [ ]not a task\n");
  auto list = std::make_unique<markdown::Checklist>();
  ASSERT_TRUE(list->load("/todo.md"));
  ASSERT_EQ(list->count, 3u);
  EXPECT_STREQ(list->tasks[0].label, "[ ] Milk");
  EXPECT_STREQ(list->tasks[1].label, "[x] Café");
  ASSERT_TRUE(list->toggle("/todo.md", 0));
  EXPECT_NE(get("/todo.md").find("- [x] Milk\r\n"), std::string::npos);
  EXPECT_NE(get("/todo.md").find("- [ ] Code"), std::string::npos);
  ASSERT_TRUE(list->toggle("/todo.md", 0));
  EXPECT_NE(get("/todo.md").find("- [ ] Milk\r\n"), std::string::npos);
}
TEST_F(MarkdownChecklistTest, PreservesLongLinesAndNoFinalNewline) {
  const std::string source = "- [ ] " + std::string(2000, 'a') + "\n- [X] Last";
  put("/todo.md", source);
  auto list = std::make_unique<markdown::Checklist>();
  ASSERT_TRUE(list->load("/todo.md"));
  ASSERT_EQ(list->count, 2u);
  ASSERT_TRUE(list->toggle("/todo.md", 1));
  std::string expected = source;
  expected[expected.rfind('X')] = ' ';
  EXPECT_EQ(get("/todo.md"), expected);
}
TEST_F(MarkdownChecklistTest, RefusesStaleOffsetsEvenWhenLengthIsUnchanged) {
  put("/todo.md", "- [ ] Milk\n");
  auto list = std::make_unique<markdown::Checklist>();
  ASSERT_TRUE(list->load("/todo.md"));
  put("/todo.md", "- [ ] Soap\n");
  EXPECT_FALSE(list->toggle("/todo.md", 0));
  EXPECT_EQ(get("/todo.md"), "- [ ] Soap\n");
}
TEST_F(MarkdownChecklistTest, WriteAndCloseFailuresKeepOriginal) {
  const std::string source = "- [ ] Milk\n";
  put("/todo.md", source);
  auto list = std::make_unique<markdown::Checklist>();
  ASSERT_TRUE(list->load("/todo.md"));
  mdtest::failWriteSuffix = ".buddy-part";
  EXPECT_FALSE(list->toggle("/todo.md", 0));
  EXPECT_EQ(get("/todo.md"), source);
  mdtest::failWriteSuffix.clear();
  mdtest::failClose = true;
  EXPECT_FALSE(list->toggle("/todo.md", 0));
  EXPECT_EQ(get("/todo.md"), source);
}
TEST_F(MarkdownChecklistTest, BoundedListAndRecovery) {
  std::string source;
  for (size_t i = 0; i < 130; ++i) source += "- [ ] Task\n";
  put("/todo.md.buddy-backup", source);
  auto list = std::make_unique<markdown::Checklist>();
  ASSERT_TRUE(list->load("/todo.md"));
  EXPECT_EQ(list->count, 128u);
  EXPECT_TRUE(list->truncated);
  EXPECT_FALSE(list->toggle("/todo.md", 128));
  EXPECT_EQ(get("/todo.md"), source);
}
TEST_F(MarkdownChecklistTest, AtomicPublishRollsBackMissingStagingFile) {
  put("/notes.md", "old notes");
  EXPECT_FALSE(atomic_file::publish("/notes.md", "/missing"));
  EXPECT_EQ(get("/notes.md"), "old notes");
  put("/new", "new notes");
  EXPECT_TRUE(atomic_file::publish("/notes.md", "/new"));
  EXPECT_EQ(get("/notes.md"), "new notes");
}

TEST_F(MarkdownChecklistTest, FailedPublicationKeepsRecoverableBackup) {
  put("/notes.md", "old notes");
  put("/new", "new notes");
  mdtest::failRenameTo = "/notes.md";
  EXPECT_FALSE(atomic_file::publish("/notes.md", "/new"));
  EXPECT_EQ(get("/notes.md.buddy-backup"), "old notes");
  mdtest::failRenameTo.clear();
  EXPECT_TRUE(atomic_file::recover("/notes.md"));
  EXPECT_EQ(get("/notes.md"), "old notes");
}
