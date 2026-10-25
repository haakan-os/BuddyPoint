#include <Ao3Client.h>
#include <Ao3WebJobs.h>
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
    EXPECT_LE(ao3test::requests[i].transmitBuffer, 2048);
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

TEST_F(Ao3ClientTest, WebJobsQueueLoginWithoutSavingPasswordAndRejectOverlap) {
  Ao3WebJobs jobs;
  EXPECT_FALSE(jobs.enqueue(Ao3WebJobs::Job::Login, "", "password"));
  EXPECT_FALSE(jobs.enqueue(Ao3WebJobs::Job::Login, "Reader", std::string(257, 'x')));
  ao3test::responses.push_back({200, "<input name='authenticity_token' value='csrf'><input name='user[password]'>"});
  ao3test::responses.push_back(
      {302, "", {{"Location", "/users/Reader"}, {"Set-Cookie", "_session=web; Path=/; Secure"}}});
  ao3test::responses.push_back({200, "<a href='/users/logout'>Log Out</a>"});
  ASSERT_TRUE(jobs.enqueue(Ao3WebJobs::Job::Login, "Reader", "secret-password"));
  EXPECT_TRUE(ao3test::requests.empty());
  EXPECT_FALSE(jobs.enqueue(Ao3WebJobs::Job::Updates));
  jobs.tick();
  EXPECT_TRUE(jobs.loginPending());
  EXPECT_TRUE(ao3test::requests.empty());
  EXPECT_TRUE(ao3test::files.empty());
  std::string user, password;
  ASSERT_TRUE(jobs.takeLogin(user, password));
  EXPECT_FALSE(jobs.busy());
  EXPECT_FALSE(jobs.loginPending());
  EXPECT_EQ(user, "Reader");
  EXPECT_EQ(password, "secret-password");
  EXPECT_FALSE(jobs.takeLogin(user, password));
  jobs.tick();
  EXPECT_TRUE(ao3test::requests.empty());
  ASSERT_TRUE(client.signIn(user, password));
  EXPECT_TRUE(client.signedIn());
  for (const auto& [path, data] : ao3test::files) EXPECT_EQ(contents(path).find("secret-password"), std::string::npos);
  ASSERT_TRUE(jobs.enqueue(Ao3WebJobs::Job::Forget));
  jobs.tick();
  EXPECT_FALSE(jobs.status().signedIn);
  EXPECT_FALSE(Storage.exists(SESSION));
}
TEST_F(Ao3ClientTest, WebJobsCancellationBeforeLoginMakesNoNetworkRequest) {
  Ao3WebJobs jobs;
  ASSERT_TRUE(jobs.enqueue(Ao3WebJobs::Job::Login, "Reader", "password"));
  jobs.cancel();
  std::string user, password;
  EXPECT_FALSE(jobs.loginPending());
  EXPECT_FALSE(jobs.takeLogin(user, password));
  jobs.tick();
  EXPECT_FALSE(jobs.busy());
  EXPECT_EQ(jobs.status().error, Ao3Client::Error::Cancelled);
  EXPECT_TRUE(ao3test::requests.empty());
  EXPECT_FALSE(Storage.exists(SESSION));
}
TEST_F(Ao3ClientTest, LoginRejectsIncompleteHeadersEvenIfStatusAndBodyLookSuccessful) {
  ao3test::responses.push_back({200, "<input name='authenticity_token' value='csrf'>"});
  ao3test::responses.back().headersComplete = false;
  EXPECT_FALSE(client.signIn("Reader", "password"));
  EXPECT_EQ(client.error(), Ao3Client::Error::Network);
  ASSERT_EQ(ao3test::requests.size(), 1U);
  EXPECT_EQ(ao3test::requests[0].method, 0);
  EXPECT_FALSE(Storage.exists(SESSION));
}
TEST_F(Ao3ClientTest, WebJobsCannotHandOffNonLoginJobs) {
  Ao3WebJobs jobs;
  std::string user, password;
  EXPECT_FALSE(jobs.takeLogin(user, password));
  ASSERT_TRUE(jobs.enqueue(Ao3WebJobs::Job::Updates));
  EXPECT_FALSE(jobs.loginPending());
  EXPECT_FALSE(jobs.takeLogin(user, password));
  EXPECT_TRUE(jobs.busy());
}
TEST_F(Ao3ClientTest, WebJobsPrivateBookmarksDownloadAndThenSkipUnchangedFics) {
  login();
  Ao3WebJobs jobs;
  const std::string listing =
      "<a href='/users/logout'>Log Out</a><ol class='bookmark index'><li class='bookmark blurb'>"
      "<h4 class='heading'><a href='/works/123'>Private fic</a></h4></li></ol>";
  const std::string detail =
      "<h2 class='title'>Private fic</h2><dd class='chapters'>2/3</dd>"
      "<a href='/downloads/123/private.epub'>EPUB</a>";
  for (int pass = 0; pass < 2; ++pass) {
    ao3test::responses.push_back({200, listing});
    ao3test::responses.push_back({200, detail});
    if (!pass) ao3test::responses.push_back({200, epub()});
    ASSERT_TRUE(jobs.enqueue(Ao3WebJobs::Job::Bookmarks));
    for (int i = 0; i < 5 && jobs.busy(); ++i) jobs.tick();
    EXPECT_FALSE(jobs.busy());
    EXPECT_EQ(jobs.status().checked, 1U);
    EXPECT_EQ(jobs.status().saved, pass ? 0U : 1U);
    EXPECT_EQ(jobs.status().unchanged, pass ? 1U : 0U);
    EXPECT_EQ(jobs.status().error, Ao3Client::Error::None);
    EXPECT_EQ(ao3test::requests.back().headers.at("Cookie"), "_session=authenticated");
  }
  EXPECT_TRUE(Storage.exists("/AO3/123.epub"));
}
TEST_F(Ao3ClientTest, WebJobsStopOnExpiredSessionAndRateLimit) {
  login();
  Ao3WebJobs jobs;
  ao3test::responses.push_back({200, "<ol class='bookmark index'></ol>"});
  ASSERT_TRUE(jobs.enqueue(Ao3WebJobs::Job::Bookmarks));
  jobs.tick();
  EXPECT_FALSE(jobs.busy());
  EXPECT_EQ(jobs.status().error, Ao3Client::Error::SignIn);
  ao3test::responses.push_back({429, "Slow down"});
  ASSERT_TRUE(jobs.enqueue(Ao3WebJobs::Job::Bookmarks));
  jobs.tick();
  EXPECT_FALSE(jobs.busy());
  EXPECT_EQ(jobs.status().error, Ao3Client::Error::RateLimit);
}
TEST_F(Ao3ClientTest, WebJobsUpdateSnapshotCancellationKeepsExistingBooks) {
  ao3test::responses.push_back({200, epub()});
  ASSERT_TRUE(client.download(work));
  Ao3WebJobs jobs;
  jobs.setObserver([](void* ptr) { static_cast<Ao3WebJobs*>(ptr)->cancel(); }, &jobs);
  ASSERT_TRUE(jobs.enqueue(Ao3WebJobs::Job::Updates));
  jobs.tick();
  EXPECT_FALSE(jobs.busy());
  EXPECT_EQ(jobs.status().error, Ao3Client::Error::Cancelled);
  EXPECT_FALSE(Storage.exists(Ao3Client::UPDATE_QUEUE));
  EXPECT_EQ(contents("/AO3/123.epub"), epub());
}
TEST_F(Ao3ClientTest, WebJobsPublicUpdatesDownloadNewChapters) {
  ao3test::responses.push_back({200, epub()});
  ASSERT_TRUE(client.download(work));
  Ao3WebJobs jobs;
  ao3test::responses.push_back({200,
                                "<h2 class='title'>Updated fic</h2><dd class='chapters'>3/3</dd>"
                                "<a href='/downloads/123/updated.epub'>EPUB</a>"});
  ao3test::responses.push_back({200, epub()});
  ASSERT_TRUE(jobs.enqueue(Ao3WebJobs::Job::Updates));
  for (int i = 0; i < 4 && jobs.busy(); ++i) jobs.tick();
  EXPECT_FALSE(jobs.busy());
  EXPECT_EQ(jobs.status().saved, 1U);
  EXPECT_FALSE(Storage.exists(Ao3Client::UPDATE_QUEUE));
  ASSERT_TRUE(client.readDownloaded("123", work));
  EXPECT_EQ(work.chapters, 3U);
}
TEST_F(Ao3ClientTest, WebJobsDestructionBeforeAnyJobDoesNotCloseAnUnopenedQueue) {
  {
    Ao3WebJobs jobs;
  }
  EXPECT_TRUE(ao3test::requests.empty());
  EXPECT_TRUE(ao3test::files.empty());
}
TEST_F(Ao3ClientTest, WebJobsDestructionWithQueuedLoginDoesNotOpenOrCloseQueue) {
  {
    Ao3WebJobs jobs;
    ASSERT_TRUE(jobs.enqueue(Ao3WebJobs::Job::Login, "Reader", "password"));
  }
  EXPECT_TRUE(ao3test::requests.empty());
  EXPECT_FALSE(Storage.exists(SESSION));
}
TEST_F(Ao3ClientTest, WebJobsUnsignedBookmarksFinishWithoutOpeningQueue) {
  Ao3WebJobs jobs;
  ASSERT_TRUE(jobs.enqueue(Ao3WebJobs::Job::Bookmarks));
  jobs.tick();
  EXPECT_FALSE(jobs.busy());
  EXPECT_EQ(jobs.status().error, Ao3Client::Error::SignIn);
  EXPECT_TRUE(ao3test::requests.empty());
}
TEST_F(Ao3ClientTest, WebJobsSnapshotFailureFinishesBeforeQueueIsOpened) {
  ao3test::responses.push_back({200, epub()});
  ASSERT_TRUE(client.download(work));
  ao3test::failWriteSuffix = "ao3-update-queue.txt";
  Ao3WebJobs jobs;
  ASSERT_TRUE(jobs.enqueue(Ao3WebJobs::Job::Updates));
  jobs.tick();
  EXPECT_FALSE(jobs.busy());
  EXPECT_EQ(jobs.status().error, Ao3Client::Error::StorageError);
  EXPECT_FALSE(Storage.exists(Ao3Client::UPDATE_QUEUE));
  EXPECT_EQ(contents("/AO3/123.epub"), epub());
}
TEST_F(Ao3ClientTest, LoginRetainsLongSessionCookiesThroughRotation) {
  const std::string before = "_session=" + std::string(3000, 'a');
  const std::string after = "_session=" + std::string(ao3::MAX_COOKIE_BYTES - 9, 'b');
  ao3test::responses.push_back({200,
                                "<input name='authenticity_token' value='csrf'><input name='user[password]'>",
                                {{"Set-Cookie", before + "; Path=/; Secure; HttpOnly"}}});
  ao3test::responses.push_back(
      {302, "", {{"Location", "/users/Reader"}, {"Set-Cookie", after + "; Path=/; Secure; HttpOnly"}}});
  ao3test::responses.push_back({200, "<a href='/users/logout'>Log Out</a>"});
  ASSERT_TRUE(client.signIn("Reader", "password"));
  ASSERT_EQ(ao3test::requests.size(), 3U);
  EXPECT_EQ(ao3test::requests[1].headers.at("Cookie"), before);
  EXPECT_EQ(ao3test::requests[2].headers.at("Cookie"), after);
  EXPECT_TRUE(client.signedIn());
  for (const auto& request : ao3test::requests) EXPECT_TRUE(request.tlsVerified);
  Ao3Client restored;
  ASSERT_TRUE(restored.loadSession());
  EXPECT_EQ(restored.account(), "Reader");
}
TEST_F(Ao3ClientTest, DownloadResizesTransmitBufferForLongRedirectWithoutDroppingSession) {
  login();
  const auto destination = std::string(ORIGIN) + "/downloads/123/" + std::string(1800, 'a') + ".epub";
  ao3test::responses.push_back({302, "", {{"Location", destination}}});
  ao3test::responses.push_back({200, epub()});
  ASSERT_TRUE(client.download(work));
  const auto& redirected = ao3test::requests.back();
  EXPECT_EQ(redirected.url, destination);
  EXPECT_EQ(redirected.headers.at("Cookie"), "_session=authenticated");
  EXPECT_TRUE(redirected.tlsVerified);
  EXPECT_LT(redirected.transmitBuffer, 6144);
  EXPECT_TRUE(ao3test::files.contains(Ao3Client::bookPath(work.id)));
}
TEST_F(Ao3ClientTest, BridgeDownloadPublishesPreparedMetadataAndNeverContactsAo3Directly) {
  client.bridgeConfig().enabled = true;
  client.bridgeConfig().defaultUrl = "http://nas.local:8080";
  client.bridgeConfig().token = std::string(64, 'b');
  const std::string job(32, 'a');
  const auto bytes = epub();
  ao3test::responses.emplace_back(202, "{\"job\":\"" + job + "\"}");
  ao3test::responses.emplace_back(200,
                                  "{\"state\":\"done\",\"result\":{\"work\":{\"id\":\"123\",\"title\":\"Updated "
                                  "fic\",\"chapters\":3},\"path\":\"/api/v1/files/" +
                                      job + "\",\"size\":" + std::to_string(bytes.size()) + "}}");
  ao3test::responses.emplace_back(200, bytes);
  ASSERT_TRUE(client.download(work));
  ao3::Work saved;
  ASSERT_TRUE(client.readDownloaded("123", saved));
  EXPECT_EQ(saved.title, "Updated fic");
  EXPECT_EQ(saved.chapters, 3U);
  for (const auto& request : ao3test::requests) {
    EXPECT_TRUE(request.url.starts_with("http://nas.local:8080/api/v1/"));
    EXPECT_FALSE(request.headers.contains("Cookie"));
  }
}
TEST_F(Ao3ClientTest, OptimizationProfileSurvivesStorageAndTriggersAnUpdate) {
  work.sourceRevision = "original";
  ao3test::responses.emplace_back(200, epub());
  ASSERT_TRUE(client.download(work));
  ao3::Work saved;
  ASSERT_TRUE(client.readDownloaded(work.id, saved));
  EXPECT_EQ(saved.sourceRevision, "original");
  EXPECT_FALSE(ao3::changed(saved, work));
  work.sourceRevision = "x3-images-v1";
  EXPECT_TRUE(ao3::changed(saved, work));
}
}  // namespace

TEST_F(Ao3ClientTest, LoginRecognizesCurrentAo3NavigationWithoutWaitingForPageTails) {
  // AO3 users/sessions/_greeting.html.erb appends return_to to the logout URL.
  const std::string form = "<input name='authenticity_token' value='csrf'><input name='user[password]'>";
  ao3test::responses.push_back({200, form + std::string(10000, ' '), {}, false});
  ao3test::responses.back().chunked = true;
  ao3test::responses.push_back(
      {302, "", {{"Location", "/users/Reader"}, {"Set-Cookie", "_session=authenticated; Path=/; Secure"}}});
  ao3test::responses.push_back(
      {200,
       "<nav id='greeting'><a href='/users/logout?return_to=%2Fusers%2FReader' data-method='delete'>Log Out</a>" +
           std::string(10000, ' '),
       {},
       false});
  ao3test::responses.back().chunked = true;
  ASSERT_TRUE(client.signIn("Reader", "password"));
  ASSERT_EQ(ao3test::requests.size(), 3U);
  EXPECT_LE(ao3test::requests[0].bytesRead, 1024U);
  EXPECT_LE(ao3test::requests[2].bytesRead, 1024U);
  Ao3Client restored;
  ASSERT_TRUE(restored.loadSession());
  EXPECT_EQ(restored.account(), "Reader");
}
TEST_F(Ao3ClientTest, ReturnedLoginFormIsStillRejectedAfterSuccessfulHttpResponse) {
  const std::string form = "<input name='authenticity_token' value='csrf'><input name='user[password]'>";
  ao3test::responses.push_back({200, form, {{"Set-Cookie", "_session=anonymous; Path=/"}}});
  ao3test::responses.push_back({200, form});
  EXPECT_FALSE(client.signIn("Reader", "wrong-password"));
  EXPECT_EQ(client.error(), Ao3Client::Error::SignIn);
  EXPECT_FALSE(Storage.exists(SESSION));
}
TEST_F(Ao3ClientTest, LogoutMarkerWithoutSessionCookieIsNotEnoughToSignIn) {
  ao3test::responses.push_back({200, "<input name='authenticity_token' value='csrf'><input name='user[password]'>"});
  ao3test::responses.push_back({200, "<a href='/users/logout?return_to=%2F'>Log Out</a>"});
  EXPECT_FALSE(client.signIn("Reader", "password"));
  EXPECT_EQ(client.error(), Ao3Client::Error::SignIn);
  EXPECT_FALSE(Storage.exists(SESSION));
}
TEST_F(Ao3ClientTest, TemporaryHeaderAndChunkedBodyWaitsResumeTheSameRequest) {
  ao3test::responses.push_back({200, epub()});
  auto& response = ao3test::responses.back();
  response.chunked = true;
  response.headerWaits = 4;
  response.readWaits = 8;
  ASSERT_TRUE(client.download(work));
  ASSERT_EQ(ao3test::requests.size(), 1U);
  const auto& request = ao3test::requests[0];
  EXPECT_TRUE(request.nonBlocking);
  EXPECT_EQ(request.readTimeout, 0);
  EXPECT_EQ(request.headerCalls, 5U);
  EXPECT_GT(request.readCalls, 8U);
  EXPECT_EQ(contents("/AO3/123.epub"), epub());
}
TEST_F(Ao3ClientTest, CancellationDuringBodyWaitKeepsExistingDownload) {
  put("/AO3/123.epub", "old epub");
  ao3test::responses.push_back({200, epub()});
  ao3test::responses.back().stallBody = true;
  client.setProgress(
      [](void*, size_t, size_t) { return ao3test::requests.empty() || ao3test::requests.back().readCalls < 3; },
      nullptr);
  EXPECT_FALSE(client.download(work));
  EXPECT_EQ(client.error(), Ao3Client::Error::Cancelled);
  EXPECT_EQ(ao3test::requests.size(), 1U);
  EXPECT_EQ(contents("/AO3/123.epub"), "old epub");
  EXPECT_FALSE(Storage.exists("/AO3/123.epub.part"));
}
TEST_F(Ao3ClientTest, CancellationDuringHeaderWaitMakesNoPostRequest) {
  ao3test::responses.push_back({200, "ignored"});
  ao3test::responses.back().headerWaits = 1000;
  client.setProgress(
      [](void*, size_t, size_t) { return ao3test::requests.empty() || ao3test::requests.back().headerCalls < 3; },
      nullptr);
  EXPECT_FALSE(client.signIn("Reader", "password"));
  EXPECT_EQ(client.error(), Ao3Client::Error::Cancelled);
  ASSERT_EQ(ao3test::requests.size(), 1U);
  EXPECT_EQ(ao3test::requests[0].method, 0);
}
TEST_F(Ao3ClientTest, EndlessHeaderWaitTimesOutWithoutRetryingLogin) {
  ao3test::responses.push_back({200, "ignored"});
  ao3test::responses.back().headerWaits = 1000;
  EXPECT_FALSE(client.signIn("Reader", "password"));
  EXPECT_EQ(client.error(), Ao3Client::Error::Timeout);
  EXPECT_EQ(ao3test::requests.size(), 1U);
  EXPECT_LT(ao3test::clock, 32000U);
}
TEST_F(Ao3ClientTest, EndlessBodyWaitTimesOutWithoutReplacingTheDownload) {
  put("/AO3/123.epub", "old epub");
  ao3test::responses.push_back({200, epub()});
  ao3test::responses.back().stallBody = true;
  EXPECT_FALSE(client.download(work));
  EXPECT_EQ(client.error(), Ao3Client::Error::Timeout);
  EXPECT_EQ(ao3test::requests.size(), 1U);
  EXPECT_EQ(contents("/AO3/123.epub"), "old epub");
  EXPECT_LT(ao3test::clock, 32000U);
}
TEST_F(Ao3ClientTest, ContinuousSlowHtmlCannotExtendTheResponseDeadlineForever) {
  ao3test::responses.push_back({200, std::string(5000, ' ')});
  ao3test::responses.back().chunkSize = 1;
  ao3test::responses.back().readAdvanceMs = 1000;
  EXPECT_FALSE(client.fetchWork("123", work));
  EXPECT_EQ(client.error(), Ao3Client::Error::Timeout);
  EXPECT_LT(ao3test::clock, 122000U);
}
TEST_F(Ao3ClientTest, LargeDownloadsCanContinueBeyondHtmlDeadlineWhileMakingProgress) {
  ao3test::responses.push_back({200, epub()});
  ao3test::responses.back().chunkSize = 32;
  ao3test::responses.back().readAdvanceMs = 15000;
  ASSERT_TRUE(client.download(work));
  EXPECT_GT(ao3test::clock, 120000U);
  EXPECT_EQ(contents("/AO3/123.epub"), epub());
}

TEST_F(Ao3ClientTest, LoginPageRetriesOne525BeforeSendingCredentials) {
  ao3test::responses.push_back({525, "TLS handshake failed", {{"Server", "cloudflare"}, {"CF-Ray", "example-BNE"}}});
  login();
  ASSERT_EQ(ao3test::requests.size(), 4U);
  EXPECT_EQ(ao3test::requests[0].method, 0);
  EXPECT_EQ(ao3test::requests[1].method, 0);
  EXPECT_GE(ao3test::requests[1].started - ao3test::requests[0].started, 5000U);
  EXPECT_TRUE(ao3test::requests[0].body.empty());
  EXPECT_TRUE(ao3test::requests[1].body.empty());
  EXPECT_EQ(ao3test::requests[2].method, HTTP_METHOD_POST);
  EXPECT_TRUE(client.signedIn());
}
TEST_F(Ao3ClientTest, Persistent525StopsAfterOneRetryWithoutSendingCredentials) {
  ao3test::responses.push_back({525, "TLS handshake failed"});
  ao3test::responses.push_back({525, "TLS handshake failed"});
  EXPECT_FALSE(client.signIn("Reader", "password"));
  EXPECT_EQ(client.error(), Ao3Client::Error::Server);
  ASSERT_EQ(ao3test::requests.size(), 2U);
  for (const auto& request : ao3test::requests) {
    EXPECT_EQ(request.method, 0);
    EXPECT_TRUE(request.body.empty());
  }
  EXPECT_FALSE(Storage.exists(SESSION));
}
TEST_F(Ao3ClientTest, LoginPostIsNotRepeatedOn525) {
  ao3test::responses.push_back({200, "<input name='authenticity_token' value='csrf'><input name='user[password]'>"});
  ao3test::responses.push_back({525, "TLS handshake failed"});
  EXPECT_FALSE(client.signIn("Reader", "password"));
  EXPECT_EQ(client.error(), Ao3Client::Error::Server);
  ASSERT_EQ(ao3test::requests.size(), 2U);
  EXPECT_EQ(ao3test::requests[1].method, HTTP_METHOD_POST);
  EXPECT_FALSE(Storage.exists(SESSION));
}
TEST_F(Ao3ClientTest, CancelDuring525BackoffPreventsTheRetry) {
  ao3test::responses.push_back({525, "TLS handshake failed"});
  client.setProgress(
      [](void*, size_t, size_t) {
        return ao3test::requests.empty() || ao3test::clock < ao3test::requests.back().started + 500;
      },
      nullptr);
  EXPECT_FALSE(client.signIn("Reader", "password"));
  EXPECT_EQ(client.error(), Ao3Client::Error::Cancelled);
  EXPECT_EQ(ao3test::requests.size(), 1U);
  EXPECT_FALSE(Storage.exists(SESSION));
}
TEST_F(Ao3ClientTest, OtherServerErrorsAreIdentifiedWithoutAutomaticRetry) {
  for (const int status : {500, 502, 503, 504, 526}) {
    const auto count = ao3test::requests.size();
    ao3test::responses.push_back({status, "Server error"});
    EXPECT_FALSE(client.fetchWork("123", work));
    EXPECT_EQ(client.error(), Ao3Client::Error::Server);
    EXPECT_EQ(ao3test::requests.size(), count + 1);
  }
}
TEST_F(Ao3ClientTest, ServerFailureStopsBookmarkSyncBeforeTheNextWork) {
  login();
  Ao3WebJobs jobs;
  ao3test::responses.push_back(
      {200,
       "<a href='/users/logout?return_to=/'>Log Out</a><ol class='bookmark index'>"
       "<li class='bookmark blurb'><h4 class='heading'><a href='/works/123'>One</a></h4></li>"
       "<li class='bookmark blurb'><h4 class='heading'><a href='/works/456'>Two</a></h4></li></ol>"});
  ao3test::responses.push_back({525, "Server error"});
  ao3test::responses.push_back({525, "Server error"});
  ASSERT_TRUE(jobs.enqueue(Ao3WebJobs::Job::Bookmarks));
  for (int i = 0; i < 5 && jobs.busy(); ++i) jobs.tick();
  EXPECT_FALSE(jobs.busy());
  EXPECT_EQ(jobs.status().error, Ao3Client::Error::Server);
  EXPECT_EQ(jobs.status().checked, 1U);
  EXPECT_EQ(jobs.status().failed, 1U);
}

TEST_F(Ao3ClientTest, ConnectionFailureDoesNotRetryOrDisableCertificateVerification) {
  ao3test::responses.push_back({200, "", {}, true, false});
  EXPECT_FALSE(client.signIn("Reader", "password"));
  EXPECT_EQ(client.error(), Ao3Client::Error::Network);
  ASSERT_EQ(ao3test::requests.size(), 1U);
  EXPECT_TRUE(ao3test::requests[0].tlsVerified);
  EXPECT_TRUE(ao3test::requests[0].body.empty());
  EXPECT_FALSE(Storage.exists(SESSION));
}
