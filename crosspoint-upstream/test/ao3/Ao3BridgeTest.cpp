#include <Ao3Bridge.h>
#include <Ao3Client.h>
#include <WiFi.h>
#include <esp_http_client.h>
#include <gtest/gtest.h>

namespace {
const std::string ID(32, 'a');
class Ao3BridgeTest : public ::testing::Test {
 protected:
  Ao3Bridge bridge;
  void SetUp() override {
    ao3test::reset();
    ao3test::requests.clear();
    ao3test::responses.clear();
    ao3test::clock = 100;
    ao3test::ssid = "Home";
    ao3test::freeHeap = 150000;
    bridge.config.enabled = true;
    bridge.config.defaultUrl = "http://nas.local:8080";
    bridge.config.token = std::string(64, 'b');
  }
  void accepted(const std::string& result) {
    ao3test::responses.emplace_back(202, "{\"job\":\"" + ID + "\"}");
    ao3test::responses.emplace_back(200, "{\"state\":\"done\",\"result\":" + result + "}");
  }
};
TEST_F(Ao3BridgeTest, NetworkOverrideUsesExactSsidAndDefaultOtherwise) {
  ASSERT_TRUE(bridge.config.setNetwork("Home", "http://192.168.1.10:8080/"));
  EXPECT_EQ(bridge.config.urlFor("Home"), "http://192.168.1.10:8080");
  EXPECT_EQ(bridge.config.urlFor("home"), "http://nas.local:8080");
  EXPECT_EQ(bridge.config.urlFor("Hotspot"), "http://nas.local:8080");
  ASSERT_TRUE(bridge.config.setNetwork("Home", ""));
  EXPECT_EQ(bridge.config.urlFor("Home"), "http://nas.local:8080");
}
TEST_F(Ao3BridgeTest, ConfigRoundTripsWithoutPlaintextToken) {
  ASSERT_TRUE(bridge.config.setNetwork("Home", "http://192.168.1.10:8080"));
  ASSERT_TRUE(bridge.config.save());
  Ao3BridgeConfig restored;
  ASSERT_TRUE(restored.load());
  EXPECT_EQ(restored.token, bridge.config.token);
  EXPECT_EQ(restored.urlFor("Home"), "http://192.168.1.10:8080");
  EXPECT_TRUE(restored.enabled);
  JsonDocument doc;
  const auto& file = ao3test::files.at("/.crosspoint/ao3-bridge.json");
  ASSERT_FALSE(deserializeJson(doc, file.data(), file.size()));
  EXPECT_TRUE(doc["token"].isNull());
  EXPECT_FALSE(doc["token_obf"].isNull());
}
TEST_F(Ao3BridgeTest, RejectsCredentialsAndQueryStringsInConfiguredUrls) {
  for (const auto* url : {"ftp://nas", "http://user:pass@nas", "http://nas?token=a", "http://nas/#x", "http:///api",
                          "http://nas:0", "http://nas:99999", "http://nas/../other", "http://nas\r\nX:bad"})
    EXPECT_FALSE(Ao3BridgeConfig::validUrl(url)) << url;
  EXPECT_TRUE(Ao3BridgeConfig::validUrl("https://bridge.example/ao3"));
}
TEST_F(Ao3BridgeTest, ConfiguredBridgeWithoutRouteFailsWithoutDirectAo3Fallback) {
  bridge.config.defaultUrl.clear();
  ao3::Work work;
  EXPECT_FALSE(bridge.fetchWork("123", work));
  EXPECT_EQ(bridge.error, ao3::Error::BridgeConfig);
  EXPECT_TRUE(ao3test::requests.empty());
}
TEST_F(Ao3BridgeTest, CorruptSavedConfigurationDoesNotEnableDirectAo3) {
  const std::string broken = "{broken";
  ao3test::files["/.crosspoint/ao3-bridge.json"] = {broken.begin(), broken.end()};
  EXPECT_FALSE(bridge.config.load());
  EXPECT_TRUE(bridge.config.enabled);
  ao3::Work work;
  EXPECT_FALSE(bridge.fetchWork("123", work));
  EXPECT_EQ(bridge.error, ao3::Error::BridgeConfig);
  EXPECT_TRUE(ao3test::requests.empty());
}
TEST_F(Ao3BridgeTest, WorkJobPollsOnSelectedNetworkAndUsesBearerHeader) {
  bridge.config.setNetwork("Home", "https://bridge.example");
  accepted(
      R"({"id":"123","title":"Private fic","chapters":2,"total":3,"profile":"x3-images-v1","download":"https://archiveofourown.org/downloads/123/work.epub?updated_at=42"})");
  ao3::Work work;
  ASSERT_TRUE(bridge.fetchWork("123", work));
  EXPECT_EQ(work.title, "Private fic");
  EXPECT_EQ(work.chapters, 2U);
  EXPECT_EQ(work.sourceRevision, "x3-images-v1");
  EXPECT_EQ(work.download, "https://archiveofourown.org/downloads/123/work.epub?updated_at=42");
  for (const auto& request : ao3test::requests) {
    EXPECT_TRUE(request.url.starts_with("https://bridge.example/api/v1/"));
    EXPECT_EQ(request.headers.at("Authorization"), "Bearer " + bridge.config.token);
    EXPECT_TRUE(request.tlsVerified);
    EXPECT_EQ(request.url.find(bridge.config.token), std::string::npos);
  }
}
TEST_F(Ao3BridgeTest, RedirectNeverReceivesBridgeToken) {
  ao3test::responses.emplace_back(
      302, "", std::vector<std::pair<std::string, std::string>>{{"Location", "http://other.example/"}});
  ao3::Work work;
  EXPECT_FALSE(bridge.fetchWork("123", work));
  EXPECT_EQ(ao3test::requests.size(), 1U);
}
TEST_F(Ao3BridgeTest, ExpiredAo3SessionIsReportedWithoutPublicFallback) {
  ao3test::responses.emplace_back(202, "{\"job\":\"" + ID + "\"}");
  ao3test::responses.emplace_back(200, R"({"state":"error","error":"sign_in"})");
  std::vector<ao3::Work> works;
  std::string next;
  EXPECT_FALSE(bridge.fetchList("https://archiveofourown.org/users/bridge/bookmarks?page=1", works, next));
  EXPECT_EQ(bridge.error, ao3::Error::SignIn);
}
TEST_F(Ao3BridgeTest, CancellationStopsPollingQueuedJob) {
  ao3test::responses.emplace_back(202, "{\"job\":\"" + ID + "\"}");
  bridge.setProgress([](void*, size_t, size_t) { return ao3test::requests.empty(); }, nullptr);
  ao3::Work work;
  EXPECT_FALSE(bridge.fetchWork("123", work));
  EXPECT_EQ(bridge.error, ao3::Error::Cancelled);
  EXPECT_LE(ao3test::requests.size(), 1U);
}
TEST_F(Ao3BridgeTest, DownloadWaitsForPreparedFileAndChecksLength) {
  accepted("{\"work\":{\"id\":\"123\",\"title\":\"Optimized\"},\"path\":\"/api/v1/files/" + ID + "\",\"size\":4}");
  ao3test::responses.emplace_back(200, "epub");
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/download.part", file));
  ao3::Work work;
  ASSERT_TRUE(bridge.download("123", file, work));
  EXPECT_EQ(file.size(), 4U);
  EXPECT_EQ(work.title, "Optimized");
}
TEST_F(Ao3BridgeTest, PreparedFileCannotRedirectTokenToAnotherOrigin) {
  accepted(R"({"work":{"id":"123","title":"Fic"},"path":"https://evil.example/file","size":4})");
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/download.part", file));
  ao3::Work work;
  EXPECT_FALSE(bridge.download("123", file, work));
  EXPECT_EQ(bridge.error, ao3::Error::Parse);
  EXPECT_EQ(ao3test::requests.size(), 2U);
}
}  // namespace
