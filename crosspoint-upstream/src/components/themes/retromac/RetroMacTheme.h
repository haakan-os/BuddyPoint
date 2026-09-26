#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

namespace RetroMacMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 15,
                                 .batteryHeight = 12,
                                 .topPadding = 5,
                                 .batteryBarHeight = 20,
                                 .headerHeight = 30,
                                 .verticalSpacing = 8,
                                 .previewPadding = 10,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 16,
                                 .listRowHeight = 28,
                                 .listWithSubtitleRowHeight = 45,
                                 .listRowGap = 0,
                                 .listRowRadius = 0,
                                 .listInset = 0,
                                 .listSidePadding = 16,
                                 .listSelectionStyle = 0,
                                 .listScrollWidth = 12,
                                 .listScrollSide = 0,
                                 .listTitleBold = false,
                                 .headerSidePadding = 8,
                                 .headerUnderlineSize = 1,
                                 .headerTitleAlign = 0,
                                 .headerBatterySide = 0,
                                 .headerBatteryDetached = false,
                                 .menuRowHeight = 34,
                                 .menuSpacing = 1,
                                 .tabSpacing = 8,
                                 .tabBarHeight = 30,
                                 .tabPillFullSlot = false,
                                 .scrollBarWidth = 12,
                                 .scrollBarRightOffset = 0,
                                 .homeTopPadding = 38,
                                 .homeCoverHeight = 230,
                                 .homeCoverTileHeight = 280,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = 12,
                                 .buttonHintsHeight = 35,
                                 .sideButtonHintsWidth = 24,
                                 .progressBarHeight = 14,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 16,
                                 .keyboardKeyHeight = 42,
                                 .keyboardKeySpacing = 2,
                                 .keyboardCenteredText = true,
                                 .keyboardVerticalOffset = 0,
                                 .keyboardTextFieldWidthPercent = 90,
                                 .keyboardWidthPercent = 96,
                                 .popupTopOffsetRatio = 0.1f,
                                 .popupMarginX = 20,
                                 .popupMarginY = 12,
                                 .popupFrameThickness = 2,
                                 .popupCornerRadius = 6,
                                 .popupTextBold = true,
                                 .popupTextInverted = true,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 6,
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
                                 .controlRadius = 4,
                                 .sheetRadius = 4,
                                 .capsuleRadius = 4};
}

class RetroMacTheme : public BaseTheme {
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
  void drawPinstripeBar(const GfxRenderer& renderer, int x, int y, int w, int h) const;
  void drawMacWindowFrame(const GfxRenderer& renderer, Rect rect, const char* title) const;
};
