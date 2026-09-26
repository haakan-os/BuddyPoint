#include <gtest/gtest.h>

#include <string>

#include "lib/JsonParser/ReleaseJsonParser.h"
#include "src/network/OtaRelease.h"

TEST(OtaRelease, UsesBuddyPointOnly) {
  EXPECT_STREQ(ota_release::latestReleaseUrl, "https://api.github.com/repos/haakan-os/BuddyPoint/releases/latest");
}

TEST(OtaRelease, StableVersionsAndOptionalTagPrefix) {
  EXPECT_TRUE(ota_release::isNewer("1.6.5", "v1.6.6"));
  EXPECT_TRUE(ota_release::isNewer("1.6.5", "1.7.0"));
  EXPECT_TRUE(ota_release::isNewer("1.6.5", "2.0.0"));
  EXPECT_FALSE(ota_release::isNewer("1.6.5", "v1.6.5"));
  EXPECT_FALSE(ota_release::isNewer("1.6.5", "1.6.4"));
  EXPECT_FALSE(ota_release::isNewer("2.0.0", "1.9.9"));
}

TEST(OtaRelease, DevelopmentAndReleaseCandidateCanMoveToStable) {
  EXPECT_TRUE(ota_release::isNewer("1.6.5-dev-develop-abcdef", "v1.6.5"));
  EXPECT_TRUE(ota_release::isNewer("1.6.5-rc+abcdef", "1.6.5"));
  EXPECT_FALSE(ota_release::isNewer("1.6.5+build", "1.6.5"));
  EXPECT_FALSE(ota_release::isNewer("1.6.6-dev-main-abc", "1.6.5"));
}

TEST(OtaRelease, MalformedAndPrereleaseTagsAreNotUpdates) {
  for (const auto* tag : {"", "latest", "v", "1.6", "1.6.6oops", "1.6.6.1", "1.6.6-rc", "4294967296.0.0"}) {
    EXPECT_FALSE(ota_release::isNewer("1.6.5", tag)) << tag;
    char name[48];
    EXPECT_FALSE(ota_release::formatAssetName(name, sizeof(name), tag, "x4", 2)) << tag;
  }
  EXPECT_FALSE(ota_release::isNewer("invalid", "1.6.6"));
}

TEST(OtaRelease, BoardSpecificAssetsAndBounds) {
  char name[48];
  ASSERT_TRUE(ota_release::formatAssetName(name, sizeof(name), "v1.6.5", "x4", 2));
  EXPECT_STREQ(name, "buddypoint-1.6.5-x3-x4.bin");
  ASSERT_TRUE(ota_release::formatAssetName(name, sizeof(name), "1.6.5", "x4pro", 5));
  EXPECT_STREQ(name, "buddypoint-1.6.5-x4pro.bin");
  EXPECT_FALSE(ota_release::formatAssetName(name, 8, "1.6.5", "x4", 2));
}

TEST(OtaRelease, StreamsGitHubReleaseAndSelectsOnlyMatchingFirmware) {
  const std::string json = R"({"tag_name":"v1.6.5","assets":[
    {"name":"buddypoint-1.6.5-x4pro.bin","size":10,"browser_download_url":"https://example.invalid/wrong"},
    {"name":"buddypoint-1.6.5-x3-x4.bin","size":5637008,"browser_download_url":"https://github.com/haakan-os/BuddyPoint/releases/download/v1.6.5/buddypoint-1.6.5-x3-x4.bin"}]})";
  ReleaseJsonParser parser;
  parser.setFirmwareAssetName("");
  bool named = false;
  for (const char byte : json) {
    parser.feed(&byte, 1);
    if (parser.foundTag() && !named) {
      char name[48];
      ASSERT_TRUE(ota_release::formatAssetName(name, sizeof(name), parser.getTagName(), "x4", 2));
      parser.setFirmwareAssetName(name);
      named = true;
    }
  }
  ASSERT_TRUE(parser.foundFirmware());
  EXPECT_EQ(parser.getFirmwareSize(), 5637008u);
  EXPECT_STREQ(parser.getFirmwareUrl(),
               "https://github.com/haakan-os/BuddyPoint/releases/download/v1.6.5/buddypoint-1.6.5-x3-x4.bin");
}
