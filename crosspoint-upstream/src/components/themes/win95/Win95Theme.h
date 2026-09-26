#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

namespace Win95Metrics {
constexpr ThemeMetrics values = {.batteryWidth = 16,
                                 .batteryHeight = 11,
                                 .topPadding = 4,
                                 .batteryBarHeight = 20,
                                 .headerHeight = 28,
                                 .verticalSpacing = 8,
                                 .previewPadding = 10,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 14,
                                 .listRowHeight = 30,
                                 .listWithSubtitleRowHeight = 48,
                                 .listRowGap = 2,
                                 .listRowRadius = 0,
                                 .listInset = 0,
                                 .listSidePadding = 14,
                                 .listSelectionStyle = 0,
                                 .listScrollWidth = 14,
                                 .listScrollSide = 0,
                                 .listTitleBold = true,
                                 .headerSidePadding = 8,
                                 .headerUnderlineSize = 1,
                                 .headerTitleAlign = 0,
                                 .headerBatterySide = 0,
                                 .headerBatteryDetached = false,
                                 .menuRowHeight = 36,
                                 .menuSpacing = 2,
                                 .tabSpacing = 6,
                                 .tabBarHeight = 28,
                                 .tabPillFullSlot = false,
                                 .scrollBarWidth = 14,
                                 .scrollBarRightOffset = 0,
                                 .homeTopPadding = 36,
                                 .homeCoverHeight = 230,
                                 .homeCoverTileHeight = 280,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = 12,
                                 .buttonHintsHeight = 36,
                                 .sideButtonHintsWidth = 24,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 4,
                                 .statusBarVerticalMargin = 14,
                                 .keyboardKeyHeight = 40,
                                 .keyboardKeySpacing = 2,
                                 .keyboardCenteredText = true,
                                 .keyboardVerticalOffset = 0,
                                 .keyboardTextFieldWidthPercent = 90,
                                 .keyboardWidthPercent = 96,
                                 .popupTopOffsetRatio = 0.1f,
                                 .popupMarginX = 16,
                                 .popupMarginY = 12,
                                 .popupFrameThickness = 2,
                                 .popupCornerRadius = 0,
                                 .popupTextBold = true,
                                 .popupTextInverted = true,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 10,
                                 .popupProgressDrawOutline = true,
                                 .popupProgressClampPercent = false,
                                 .popupProgressFillInverted = true,
                                 .popupProgressOutlineInverted = true,
                                 .optionPopupItemSpacing = 0,
                                 .optionPopupInnerPadding = 12,
                                 .optionPopupSelectionVPadding = 2,
                                 .optionPopupDialogSideMargin = 16,
                                 .textFieldHorizontalPadding = 4,
                                 .textFieldNormalThickness = 1,
                                 .textFieldCursorThickness = 2,
                                 .textFieldLineEndOffset = 0,
                                 .controlRadius = 0,
                                 .sheetRadius = 0,
                                 .capsuleRadius = 0};
}  // namespace Win95Metrics

class Win95Theme : public BaseTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  int getMenuRowHeight(const GfxRenderer& renderer) const override;
  ButtonMenuLayout getButtonMenuLayout(const GfxRenderer& renderer, Rect rect, int buttonCount,
                                       int selectedIndex) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                     const char* rightLabel = nullptr) const override;
  void fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const override;
  bool showsFileIcons() const override { return true; }

 private:
  void draw3DBevel(const GfxRenderer& renderer, int x, int y, int w, int h, bool sunken) const;
  void drawWin95WindowFrame(const GfxRenderer& renderer, Rect rect, const char* title, bool active) const;
};
