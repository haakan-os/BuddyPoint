#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <string>

#include "activities/sync/BuddySyncState.h"

using Update = BuddySyncTransferState::Update;

TEST(BuddySync, CompletionIsDetectedEvenWhenTheWholeUploadHappensBetweenLoops) {
  BuddySyncTransferState state;
  EXPECT_EQ(state.observe(0, false, false, 0, 0, 0), Update::None);
  EXPECT_EQ(state.observe(1, false, true, 10, 512, 800), Update::Succeeded);
  EXPECT_EQ(state.getPercent(), 100);
  EXPECT_EQ(state.observe(1, false, true, 20, 512, 800), Update::None);
}

TEST(BuddySync, ResendingTheSameBookProducesAnotherCompletion) {
  BuddySyncTransferState state;
  EXPECT_EQ(state.observe(1, false, true, 10, 512, 800), Update::Succeeded);
  EXPECT_EQ(state.observe(2, false, true, 20, 512, 800), Update::Succeeded);
  EXPECT_EQ(state.observe(2, false, true, 30, 512, 800), Update::None);
}

TEST(BuddySync, FailureNeverReportsOneHundredPercentAndCanBeRetried) {
  BuddySyncTransferState state;
  EXPECT_EQ(state.observe(1, true, false, 0, 0, 1000), Update::Progress);
  EXPECT_EQ(state.observe(1, true, false, 1600, 999, 1000), Update::Progress);
  EXPECT_EQ(state.observe(1, false, false, 1700, 999, 1000), Update::Failed);
  EXPECT_EQ(state.getPercent(), 0);
  EXPECT_EQ(state.observe(1, false, false, 1800, 999, 1000), Update::None);
  EXPECT_EQ(state.observe(2, true, false, 1801, 0, 1000), Update::Progress);
  EXPECT_EQ(state.getPercent(), 0);
  EXPECT_EQ(state.observe(2, false, true, 1802, 1000, 1000), Update::Succeeded);
}

TEST(BuddySync, StartFailureIsReportedWithoutReceivingAnyBytes) {
  BuddySyncTransferState state;
  EXPECT_EQ(state.observe(1, false, false, 0, 0, 0), Update::Failed);
  EXPECT_EQ(state.getPercent(), 0);
}

TEST(BuddySync, FastProgressJumpsDoNotFloodTheEinkDisplay) {
  BuddySyncTransferState state;
  EXPECT_EQ(state.observe(1, true, false, 0, 0, 100), Update::Progress);
  for (int percent = 10; percent < 100; percent += 10) {
    EXPECT_EQ(state.observe(1, true, false, percent, percent, 100), Update::None);
  }
  EXPECT_EQ(state.observe(1, true, false, 1500, 100, 100), Update::Progress);
  EXPECT_EQ(state.getPercent(), 99);
  EXPECT_EQ(state.observe(1, false, true, 1501, 100, 100), Update::Succeeded);
  EXPECT_EQ(state.getPercent(), 100);
}

TEST(BuddySync, ClockRolloverDoesNotStopProgressUpdates) {
  BuddySyncTransferState state;
  EXPECT_EQ(state.observe(1, true, false, UINT32_MAX - 499, 0, 100), Update::Progress);
  EXPECT_EQ(state.observe(1, true, false, 999, 50, 100), Update::None);
  EXPECT_EQ(state.observe(1, true, false, 1000, 50, 100), Update::Progress);
}

TEST(BuddySync, LargeAndUnknownUploadSizesAreHandled) {
  BuddySyncTransferState state;
  EXPECT_EQ(state.observe(1, true, false, 0, UINT32_MAX, UINT32_MAX), Update::Progress);
  EXPECT_EQ(state.getPercent(), 99);
  EXPECT_EQ(state.observe(2, true, false, 1, 100, 0), Update::Progress);
  EXPECT_EQ(state.getPercent(), 0);
}

TEST(BuddySync, LongSessionsKeepOnlyTheLatestEightLogEntries) {
  BuddySyncLog log;
  for (int i = 0; i < 10000; ++i) {
    const auto message = std::to_string(i);
    log.add(message.c_str(), i % 2 == 0);
    ASSERT_LE(log.size(), BuddySyncLog::capacity);
  }
  ASSERT_EQ(log.size(), 8);
  for (size_t i = 0; i < log.size(); ++i) {
    EXPECT_STREQ(log[i].text, std::to_string(9992 + i).c_str());
    EXPECT_EQ(log[i].success, i % 2 == 0);
  }
  log.clear();
  EXPECT_EQ(log.size(), 0);
  log.add("Ready", true);
  EXPECT_STREQ(log[0].text, "Ready");
}

TEST(BuddySync, VeryLongFilenamesCannotGrowTheLog) {
  BuddySyncLog log;
  std::string longName(10000, 'a');
  log.add(longName.c_str(), false);
  EXPECT_EQ(std::strlen(log[0].text), sizeof(log[0].text) - 1);
  log.add("Next book", true);
  EXPECT_STREQ(log[1].text, "Next book");
}

TEST(BuddySync, TruncatedBookTitlesKeepCompleteUnicodeCharacters) {
  BuddySyncLog log;
  const std::string title = std::string(158, 'a') + "\xc3\xa9.epub";
  log.add(title.c_str(), true);
  EXPECT_EQ(std::strlen(log[0].text), 158);
  const std::string fitting = std::string(157, 'a') + "\xc3\xa9";
  log.add(fitting.c_str(), true);
  EXPECT_STREQ(log[1].text, fitting.c_str());
}
