#include <gtest/gtest.h>

#include "components/themes/BaseTheme.h"

TEST(ThemeMenuLayout, PortraitShowsAllActionsWithoutScrolling) {
  for (int sideMargin : {14, 16}) {
    for (int count : {5, 6}) {
      for (int selected = -1; selected < count; ++selected) {
        const auto menu = ButtonMenuLayout::window(Rect{0, 328, 528, 420}, sideMargin, 36, count, selected);
        EXPECT_EQ(menu.firstVisible, 0);
        EXPECT_EQ(menu.visibleCount, count);
        EXPECT_EQ(menu.bounds.y, 354);
        EXPECT_EQ(menu.bounds.x, sideMargin + 4);
        EXPECT_EQ(menu.bounds.width, 528 - 2 * sideMargin - 8);
      }
    }
  }
}

TEST(ThemeMenuLayout, EveryLandscapeSelectionIsVisibleAndInsideWindow) {
  for (int sideMargin : {14, 16}) {
    for (int rowHeight : {32, 36, 40, 48}) {
      for (int count : {5, 6}) {
        // X3 landscape, with the existing cover, title bar and button hints.
        const Rect window{0, 328, 792, 148};
        for (int selected = 0; selected < count; ++selected) {
          const auto menu = ButtonMenuLayout::window(window, sideMargin, rowHeight, count, selected);
          EXPECT_GT(menu.visibleCount, 0);
          EXPECT_LE(menu.firstVisible, selected);
          EXPECT_GT(menu.firstVisible + menu.visibleCount, selected);
          EXPECT_LE(menu.firstVisible + menu.visibleCount, count);
          EXPECT_GE(menu.bounds.y, window.y + 26);
          EXPECT_LE(menu.bounds.y + menu.visibleCount * menu.rowHeight, window.y + window.height - 4);
          EXPECT_EQ(menu.rowSpacing, 0);
          EXPECT_EQ(menu.bounds.width, 792 - 2 * sideMargin - 8 - 12);
        }
      }
    }
  }
}

TEST(ThemeMenuLayout, TouchDownDoesNotChangeTheActionUnderTheFinger) {
  for (int count : {5, 6}) {
    for (int selected = 0; selected < count; ++selected) {
      const Rect window{0, 328, 792, 148};
      const auto down = ButtonMenuLayout::window(window, 14, 36, count, selected);
      for (int row = 0; row < down.visibleCount; ++row) {
        const int action = down.firstVisible + row;
        const auto release = ButtonMenuLayout::window(window, 14, 36, count, action);
        EXPECT_EQ(release.firstVisible + row, action);
        EXPECT_EQ(release.visibleCount, down.visibleCount);
        EXPECT_EQ(release.bounds.y, down.bounds.y);
      }
    }
  }
}

TEST(ThemeMenuLayout, CoverFocusAndNavigationWrapReturnToFirstPage) {
  const Rect window{0, 328, 792, 148};
  const auto last = ButtonMenuLayout::window(window, 14, 36, 6, 5);
  EXPECT_GT(last.firstVisible, 0);
  for (int selected : {-1, 0}) {
    const auto first = ButtonMenuLayout::window(window, 14, 36, 6, selected);
    EXPECT_EQ(first.firstVisible, 0);
    EXPECT_EQ(first.visibleCount, 3);
  }
}

TEST(ThemeMenuLayout, ExactFitIncludesLastRowButPartialRowsAreNotTouchable) {
  const auto exact = ButtonMenuLayout::window(Rect{0, 0, 528, 102}, 14, 36, 6, 0);
  EXPECT_EQ(exact.visibleCount, 2);
  const auto partial = ButtonMenuLayout::window(Rect{0, 0, 528, 101}, 14, 36, 6, 0);
  EXPECT_EQ(partial.visibleCount, 1);
}

TEST(ThemeMenuLayout, EmptyOrInsufficientSpaceProducesNoRows) {
  for (int height : {-10, 0, 20, 30, 65}) {
    EXPECT_EQ(ButtonMenuLayout::window(Rect{0, 0, 528, height}, 14, 36, 6, 0).visibleCount, 0);
  }
  EXPECT_EQ(ButtonMenuLayout::window(Rect{0, 0, 528, 400}, 14, 36, 0, -1).visibleCount, 0);
  EXPECT_EQ(ButtonMenuLayout::window(Rect{0, 0, 20, 400}, 14, 36, 6, 0).visibleCount, 0);
  EXPECT_EQ(ButtonMenuLayout::window(Rect{0, 0, 528, 400}, 14, 0, 6, 0).visibleCount, 0);
}

TEST(ThemeMenuLayout, LastPageContainsOnlyRemainingActions) {
  const auto menu = ButtonMenuLayout::window(Rect{0, 328, 792, 148}, 14, 36, 5, 4);
  EXPECT_EQ(menu.firstVisible, 3);
  EXPECT_EQ(menu.visibleCount, 2);
}
