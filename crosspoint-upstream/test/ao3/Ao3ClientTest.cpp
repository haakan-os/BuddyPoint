#include <Ao3Client.h>
#include <Epub.h>
#include <esp_http_client.h>
#include <gtest/gtest.h>
#include <zlib.h>

namespace {
constexpr const char* ORIGIN = "https://archiveofourown.org";
constexpr const char* SESSION = "/.crosspoint/ao3-session.json";
std::string contents(const std::string& path) {
  const auto& bytes = ao3test::files.at(path);
  return {bytes.begin(), bytes.end()};
}
void put(const std::string& path, const std::string& value) { ao3test::files[path] = {value.begin(), value.end()}; }
void le(std::string& bytes, uint32_t value, unsigned count) {
  for (unsigned i = 0; i < count; ++i) bytes += static_cast<char>((value >> (i * 8)) & 255);
}
// A small real ZIP exercises the firmware's ZipFile validation, not a ZIP mock.
std::string epub() {
  const std::vector<std::pair<std::string, std::string>> entries = {
      {"mimetype", "application/epub+zip"},
      {"META-INF/container.xml",
       "<?xml version='1.0'?><container><rootfiles><rootfile full-path='content.opf'/></rootfiles></container>"},
      {"content.opf", "<package><metadata/><manifest/><spine/></package>"}};
  std::string bytes, central;
  for (const auto& [name, body] : entries) {
    const auto offset = bytes.size();
    const auto crc = crc32(0, reinterpret_cast<const uint8_t*>(body.data()), body.size());
    le(bytes, 0x04034b50, 4);
    le(bytes, 20, 2);
    le(bytes, 0, 2);
    le(bytes, 0, 2);
    le(bytes, 0, 4);
    le(bytes, crc, 4);
    le(bytes, body.size(), 4);
    le(bytes, body.size(), 4);
    le(bytes, name.size(), 2);
    le(bytes, 0, 2);
    bytes += name;
    bytes += body;
    le(central, 0x02014b50, 4);
    le(central, 20, 2);
    le(central, 20, 2);
    le(central, 0, 2);
    le(central, 0, 2);
    le(central, 0, 4);
    le(central, crc, 4);
    le(central, body.size(), 4);
    le(central, body.size(), 4);
    le(central, name.size(), 2);
    le(central, 0, 2);
    le(central, 0, 2);
    le(central, 0, 2);
    le(central, 0, 2);
    le(central, 0, 4);
    le(central, offset, 4);
    central += name;
  }
  const auto offset = bytes.size();
  bytes += central;
  le(bytes, 0x06054b50, 4);
  le(bytes, 0, 2);
  le(bytes, 0, 2);
  le(bytes, entries.size(), 2);
  le(bytes, entries.size(), 2);
  le(bytes, central.size(), 4);
  le(bytes, offset, 4);
  le(bytes, 0, 2);
  return bytes;
}
class Ao3ClientTest : public ::testing::Test {
 protected:
  Ao3Client client;
  ao3::Work work;
  void SetUp() override {
    ao3test::reset();
    ao3test::requests.clear();
    ao3test::responses.clear();
    ao3test::clock = 100;
    ao3test::freeHeap = 150000;
    work.id = "123";
    work.title = "Test fic";
    work.author = "Writer";
    work.chapters = 2;
    work.words = 200;
    work.updated = "2026-10-25";
    work.download = std::string(ORIGIN) + "/downloads/123/fic.epub?updated_at=42";
  }
  void login() {
    ao3test::responses.push_back({200,
                                  "<input name='authenticity_token' value='csrf+&='><input name='user[password]'>",
                                  {{"Set-Cookie", "_session=pre; Path=/; Secure"}}});
    ao3test::responses.push_back(
        {302, "", {{"Location", "/users/Reader"}, {"Set-Cookie", "_session=authenticated; Path=/; Secure; HttpOnly"}}});
    ao3test::responses.push_back({200, "<a href='/users/logout'>Log Out</a>"});
    ASSERT_TRUE(client.signIn("Reader", "private+password&="));
  }
};
TEST_F(Ao3ClientTest, LoginUsesCsrfVerifiedTlsCookiesAndPersistsNoPassword) {
  login();
  ASSERT_EQ(ao3test::requests.size(), 3U);
  const auto& post = ao3test::requests[1];
  EXPECT_EQ(post.method, HTTP_METHOD_POST);
  EXPECT_NE(post.body.find("authenticity_token=csrf%2B%26%3D"), std::string::npos);
  EXPECT_NE(post.body.find("user[password]=private%2Bpassword%26%3D"), std::string::npos);
  EXPECT_EQ(post.headers.at("Cookie"), "_session=pre");
  EXPECT_EQ(ao3test::requests[2].method, 0);
  EXPECT_TRUE(ao3test::requests[2].body.empty());
  EXPECT_EQ(ao3test::requests[2].headers.at("Cookie"), "_session=authenticated");
  for (size_t i = 0; i < ao3test::requests.size(); ++i) {
    EXPECT_TRUE(ao3test::requests[i].tlsVerified);
    EXPECT_TRUE(ao3test::requests[i].manualRedirect);
    if (i) {
      EXPECT_GE(ao3test::requests[i].started - ao3test::requests[i - 1].started, 2000U);
    }
  }
  for (const auto& [path, data] : ao3test::files) EXPECT_EQ(contents(path).find("password"), std::string::npos);
  Ao3Client restored;
  ASSERT_TRUE(restored.loadSession());
  EXPECT_EQ(restored.account(), "Reader");
  ASSERT_TRUE(restored.signOut());
  EXPECT_FALSE(ao3test::files.contains(SESSION));
  EXPECT_FALSE(restored.signedIn());
}
TEST_F(Ao3ClientTest, BookmarkRequestKeepsAuthenticationAndRejectsExpiredPublicFallback) {
  login();
  std::vector<ao3::Work> works;
  std::string next;
  const std::string url = std::string(ORIGIN) + "/users/Reader/bookmarks";
  ao3test::responses.push_back({200,
                                "<a href='/users/logout'>Log Out</a><ol class='bookmark index'><li class='bookmark "
                                "blurb'><h4 class='heading'><a href='/works/123'>Private fic</a></h4></li></ol>"});
  ASSERT_TRUE(client.fetchList(url, true, works, next));
  ASSERT_EQ(works.size(), 1U);
  EXPECT_EQ(works[0].title, "Private fic");
  EXPECT_EQ(ao3test::requests.back().headers.at("Cookie"), "_session=authenticated");
  ao3test::responses.push_back({200, "<ol class='bookmark index'></ol>"});
  EXPECT_FALSE(client.fetchList(url, true, works, next));
  EXPECT_EQ(client.error(), Ao3Client::Error::SignIn);
}
TEST_F(Ao3ClientTest, LockedWorkAndEpubUseTheSameSignedInSession) {
  login();
  ao3test::responses.push_back(
      {200,
       "<h2 class='title'>Locked fic</h2><dd class='chapters'>2/3</dd><a href='/downloads/123/locked.epub'>EPUB</a>"});
  ASSERT_TRUE(client.fetchWork("123", work));
  ao3test::responses.push_back({200, epub()});
  ASSERT_TRUE(client.download(work));
  EXPECT_EQ(ao3test::requests.back().url, std::string(ORIGIN) + "/downloads/123/locked.epub");
  EXPECT_EQ(ao3test::requests.back().headers.at("Cookie"), "_session=authenticated");
}
TEST_F(Ao3ClientTest, ExternalRedirectNeverReceivesCredentials) {
  login();
  ao3test::responses.push_back({302, "", {{"Location", "https://elsewhere.example/steal"}}});
  const auto before = ao3test::requests.size();
  EXPECT_FALSE(client.fetchWork("123", work));
  EXPECT_EQ(ao3test::requests.size(), before + 1);
  EXPECT_EQ(client.error(), Ao3Client::Error::Network);
}
TEST_F(Ao3ClientTest, RateLimitIsNotRetried) {
  ao3test::responses.push_back({429, "Slow down"});
  EXPECT_FALSE(client.fetchWork("123", work));
  EXPECT_EQ(client.error(), Ao3Client::Error::RateLimit);
  EXPECT_EQ(ao3test::requests.size(), 1U);
}
TEST_F(Ao3ClientTest, ReplacementPreservesReadingPositionAndInvalidatesDerivedCache) {
  const auto path = Ao3Client::bookPath(work.id);
  put(path, "old epub");
  const auto cache = Epub(path, "/.crosspoint").getCachePath();
  ao3test::directories.insert(cache);
  ao3test::directories.insert(cache + "/sections");
  put(cache + "/progress.bin", "position");
  put(cache + "/cover.bmp", "cover");
  put(cache + "/sections/1.bin", "stale");
  ao3test::responses.push_back({200, epub()});
  ASSERT_TRUE(client.download(work));
  EXPECT_EQ(contents(path), epub());
  EXPECT_EQ(contents(cache + "/progress.bin"), "position");
  EXPECT_FALSE(Storage.exists((cache + "/cover.bmp").c_str()));
  EXPECT_FALSE(Storage.exists((cache + "/sections").c_str()));
  EXPECT_FALSE(Storage.exists((path + ".part").c_str()));
  EXPECT_FALSE(Storage.exists((path + ".buddy-backup").c_str()));
  ao3::Work restored;
  ASSERT_TRUE(client.readDownloaded("123", restored));
  EXPECT_EQ(restored.title, work.title);
  EXPECT_EQ(restored.chapters, 2U);
  EXPECT_EQ(restored.download, work.download);
}
TEST_F(Ao3ClientTest, TruncatedTransferKeepsExistingEpubAndRecord) {
  const auto path = Ao3Client::bookPath(work.id);
  put(path, "old epub");
  put("/.crosspoint/ao3/123.json", "old metadata");
  ao3test::responses.push_back({200, epub(), {}, false});
  EXPECT_FALSE(client.download(work));
  EXPECT_EQ(client.error(), Ao3Client::Error::Network);
  EXPECT_EQ(contents(path), "old epub");
  EXPECT_EQ(contents("/.crosspoint/ao3/123.json"), "old metadata");
  EXPECT_FALSE(Storage.exists((path + ".part").c_str()));
}
TEST_F(Ao3ClientTest, HtmlAndTruncatedArchivesNeverReplaceTheLocalBook) {
  for (const auto& invalid : {std::string("<html>Sign in</html>"), epub().substr(0, 100)}) {
    put(Ao3Client::bookPath(work.id), "old epub");
    ao3test::responses.push_back({200, invalid});
    EXPECT_FALSE(client.download(work));
    EXPECT_EQ(client.error(), Ao3Client::Error::Parse);
    EXPECT_EQ(contents(Ao3Client::bookPath(work.id)), "old epub");
  }
}
TEST_F(Ao3ClientTest, CancelAndSdWriteFailureKeepTheLocalBook) {
  const auto path = Ao3Client::bookPath(work.id);
  put(path, "old epub");
  ao3test::responses.push_back({200, epub()});
  client.setProgress([](void*, size_t bytes, size_t) { return bytes == 0; }, nullptr);
  EXPECT_FALSE(client.download(work));
  EXPECT_EQ(client.error(), Ao3Client::Error::Cancelled);
  EXPECT_EQ(contents(path), "old epub");
  client.setProgress(nullptr, nullptr);
  ao3test::failWriteSuffix = ".part";
  ao3test::responses.push_back({200, epub()});
  EXPECT_FALSE(client.download(work));
  EXPECT_EQ(client.error(), Ao3Client::Error::StorageError);
  EXPECT_EQ(contents(path), "old epub");
}
TEST_F(Ao3ClientTest, FailedPublishRestoresThePreviousBook) {
  const auto path = Ao3Client::bookPath(work.id);
  put(path, "old epub");
  ao3test::failRenameTo = path;
  ao3test::responses.push_back({200, epub()});
  EXPECT_FALSE(client.download(work));
  EXPECT_EQ(client.error(), Ao3Client::Error::StorageError);
  EXPECT_EQ(contents(path), "old epub");
  EXPECT_FALSE(Storage.exists((path + ".buddy-backup").c_str()));
}
TEST_F(Ao3ClientTest, DownloadLoginRedirectIsAnAuthenticationError) {
  put(Ao3Client::bookPath(work.id), "old epub");
  ao3test::responses.push_back({302, "", {{"Location", "/users/login"}}});
  EXPECT_FALSE(client.download(work));
  EXPECT_EQ(client.error(), Ao3Client::Error::SignIn);
  EXPECT_EQ(contents(Ao3Client::bookPath(work.id)), "old epub");
  EXPECT_EQ(ao3test::requests.size(), 1U);
}
TEST_F(Ao3ClientTest, RotatedSessionIsSavedButUnchangedSessionDoesNotRewriteSd) {
  login();
  const std::string html = "<h2 class='title'>Fic</h2><dd class='chapters'>1/1</dd>";
  const auto writes = ao3test::writes;
  ao3test::responses.push_back({200, html});
  ASSERT_TRUE(client.fetchWork("123", work));
  EXPECT_EQ(ao3test::writes, writes);
  ao3test::responses.push_back({200, html, {{"Set-Cookie", "_session=rotated; Secure; Path=/"}}});
  ASSERT_TRUE(client.fetchWork("123", work));
  EXPECT_GT(ao3test::writes, writes);
  EXPECT_NE(contents(SESSION).find("rotated"), std::string::npos);
}
TEST_F(Ao3ClientTest, UpdateQueueSurvivesMetadataReplacementAndLibraryPaginates) {
  for (unsigned id = 1; id <= 22; ++id) {
    work.id = std::to_string(id);
    work.download = std::string(ORIGIN) + "/downloads/" + work.id + "/fic.epub";
    ao3test::responses.push_back({200, epub()});
    ASSERT_TRUE(client.download(work));
  }
  ASSERT_TRUE(client.snapshotDownloads());
  const auto snapshot = contents(Ao3Client::UPDATE_QUEUE);
  std::vector<ao3::Work> works;
  bool more = false;
  ASSERT_TRUE(client.downloadedPage(0, works, more));
  EXPECT_EQ(works.size(), 20U);
  EXPECT_TRUE(more);
  ASSERT_TRUE(client.downloadedPage(20, works, more));
  EXPECT_EQ(works.size(), 2U);
  EXPECT_FALSE(more);
  ++work.chapters;
  ao3test::responses.push_back({200, epub()});
  ASSERT_TRUE(client.download(work));
  EXPECT_EQ(contents(Ao3Client::UPDATE_QUEUE), snapshot);
}
TEST_F(Ao3ClientTest, LowHeapFailsBeforeOpeningTls) {
  ao3test::freeHeap = 50000;
  ao3test::responses.push_back({200, "unused"});
  EXPECT_FALSE(client.fetchWork("123", work));
  EXPECT_EQ(client.error(), Ao3Client::Error::Memory);
  EXPECT_TRUE(ao3test::requests.empty());
}
TEST_F(Ao3ClientTest, ReadingStatusTracksOpeningEndOfWorkAndNewDownloads) {
  ao3test::responses.push_back({200, epub()});
  ASSERT_TRUE(client.download(work));
  ao3::Work saved;
  ASSERT_TRUE(client.readDownloaded(work.id, saved));
  EXPECT_EQ(saved.readingStatus, ao3::ReadingStatus::Unread);
  const auto path = Ao3Client::bookPath(work.id);
  ASSERT_TRUE(Ao3Client::markOpened(path));
  const auto writes = ao3test::writes;
  ASSERT_TRUE(Ao3Client::markOpened(path));
  EXPECT_EQ(ao3test::writes, writes);
  ASSERT_TRUE(client.readDownloaded(work.id, saved));
  EXPECT_EQ(saved.readingStatus, ao3::ReadingStatus::Reading);
  const auto cache = Epub(path, "/.crosspoint").getCachePath();
  ao3test::directories.insert(cache);
  put(cache + "/progress.bin", std::string("\x03\0\0\0", 4));
  put(cache + "/book.bin", std::string("\0\0\0\0\0\x03\0", 7));
  ASSERT_TRUE(client.readDownloaded(work.id, saved));
  EXPECT_EQ(saved.readingStatus, ao3::ReadingStatus::Waiting);
  work.chapters = work.totalChapters = 3;
  ao3test::responses.push_back({200, epub()});
  ASSERT_TRUE(client.download(work));
  ASSERT_TRUE(client.readDownloaded(work.id, saved));
  EXPECT_EQ(saved.readingStatus, ao3::ReadingStatus::NewChapters);
  ASSERT_TRUE(Ao3Client::markOpened(path));
  // Simulate the reader rebuilding its cache and reaching the new final spine.
  put(cache + "/progress.bin", std::string("\x04\0\0\0", 4));
  put(cache + "/book.bin", std::string("\0\0\0\0\0\x04\0", 7));
  ASSERT_TRUE(client.readDownloaded(work.id, saved));
  EXPECT_EQ(saved.readingStatus, ao3::ReadingStatus::Finished);
}
TEST_F(Ao3ClientTest, WarningsAndTagsSurviveAnOfflineReload) {
  work.warnings = "Creator Chose Not To Use Archive Warnings";
  work.tags = "Fluff · Angst";
  ao3test::responses.push_back({200, epub()});
  ASSERT_TRUE(client.download(work));
  ao3::Work saved;
  ASSERT_TRUE(client.readDownloaded(work.id, saved));
  EXPECT_EQ(saved.warnings, work.warnings);
  EXPECT_EQ(saved.tags, work.tags);
}
}  // namespace
