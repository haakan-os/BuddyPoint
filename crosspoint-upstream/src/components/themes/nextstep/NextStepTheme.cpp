#include "NextStepTheme.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/blocks.h"
#include "components/icons/book.h"
#include "components/icons/bookmark.h"
#include "components/icons/folder.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

namespace {

const uint8_t* iconForName(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Wifi:
      return WifiIcon;
    case UIIcon::Hotspot:
      return HotspotIcon;
    case UIIcon::Bookmark:
      return BookmarkIcon;
    case UIIcon::Blocks:
      return BlocksIcon;
    default:
      return nullptr;
  }
}

// Custom icon plotter supporting both black and white ink
void drawIconColored(const GfxRenderer& renderer, const uint8_t bitmap[], int x, int y, int size, bool color) {
  if (!bitmap) return;
  const int rowBytes = (size + 7) / 8;
  for (int row = 0; row < size; row++) {
    for (int col = 0; col < size; col++) {
      const uint8_t byte = bitmap[row * rowBytes + (col >> 3)];
      const bool ink = ((byte >> (7 - (col & 7))) & 1) == 0;
      if (ink) {
        renderer.drawPixel(x + (size - 1 - row), y + col, color);
      }
    }
  }
}

}  // namespace

void NextStepTheme::drawNeXTBevel(const GfxRenderer& renderer, int x, int y, int w, int h, bool sunken) const {
  // 1px Outer black boundary
  renderer.drawRect(x, y, w, h, true);

  if (!sunken) {
    // Raised NeXT 3D Bevel: White highlight on top/left, black shadow on bottom/right
    renderer.drawLine(x + 1, y + 1, x + w - 2, y + 1, 1, false);         // Top white
    renderer.drawLine(x + 1, y + 1, x + 1, y + h - 2, 1, false);         // Left white
    renderer.drawLine(x + 1, y + h - 2, x + w - 2, y + h - 2, 1, true);  // Bottom dark
    renderer.drawLine(x + w - 2, y + 1, x + w - 2, y + h - 2, 1, true);  // Right dark
  } else {
    // Sunken NeXT 3D Bevel: Dark shadow on top/left, white highlight on bottom/right
    renderer.drawLine(x + 1, y + 1, x + w - 2, y + 1, 1, true);           // Top dark
    renderer.drawLine(x + 1, y + 1, x + 1, y + h - 2, 1, true);           // Left dark
    renderer.drawLine(x + 1, y + h - 2, x + w - 2, y + h - 2, 1, false);  // Bottom white
    renderer.drawLine(x + w - 2, y + 1, x + w - 2, y + h - 2, 1, false);  // Right white
  }
}

void NextStepTheme::drawNeXTWindowFrame(const GfxRenderer& renderer, Rect rect, const char* title, bool active) const {
  // 1. Raised NeXT 3D outer window border
  drawNeXTBevel(renderer, rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4, true);

  // 2. NeXT dark title bar
  const int tbX = rect.x + 3;
  const int tbY = rect.y + 3;
  const int tbW = rect.width - 6;
  const int tbH = 22;

  if (active) {
    // Solid black active title bar (iconic NeXT look)
    renderer.fillRect(tbX, tbY, tbW, tbH, true);
  } else {
    // Inactive title bar: dithered gray
    renderer.fillRectDither(tbX, tbY, tbW, tbH, Color::LightGray);
  }

  // Mini NeXT cube logo on the title bar left
  const int cubeX = tbX + 5;
  const int cubeY = tbY + 4;
  renderer.drawRect(cubeX, cubeY, 13, 13, !active);
  renderer.fillRect(cubeX + 2, cubeY + 2, 4, 4, !active);
  renderer.fillRect(cubeX + 7, cubeY + 7, 4, 4, !active);

  // Title text
  if (title && title[0] != '\0') {
    const int textY = tbY + (tbH - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, cubeX + 18, textY, title, !active, EpdFontFamily::BOLD);
  }

  // 3. NeXT Miniaturize & Close controls on the right
  const int btnSize = 14;
  const int btnY = tbY + 4;

  // Close [X] box on far right
  const int closeX = tbX + tbW - btnSize - 3;
  drawNeXTBevel(renderer, closeX, btnY, btnSize, btnSize, false);
  renderer.drawLine(closeX + 3, btnY + 3, closeX + 9, btnY + 9, 1, true);
  renderer.drawLine(closeX + 9, btnY + 3, closeX + 3, btnY + 9, 1, true);

  // Miniaturize box
  const int minX = closeX - btnSize - 2;
  drawNeXTBevel(renderer, minX, btnY, btnSize, btnSize, false);
  renderer.drawRect(minX + 3, btnY + 3, 7, 7, true);
  renderer.fillRect(minX + 5, btnY + 5, 3, 3, true);
}

void NextStepTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  const int screenWidth = renderer.getScreenWidth();
  const int barHeight = 28;

  // NeXTSTEP Menu Bar: solid white with dark 1px bottom border
  renderer.fillRect(0, 0, screenWidth, barHeight, false);
  renderer.drawLine(0, barHeight - 1, screenWidth - 1, barHeight - 1, 1, true);

  // NeXT cube badge on left
  renderer.fillRect(10, 5, 18, 18, true);
  renderer.fillRect(12, 7, 6, 6, false);
  renderer.fillRect(20, 15, 6, 6, false);

  const int textY = (barHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  renderer.drawText(UI_10_FONT_ID, 34, textY, "NeXTSTEP  |  Workspace", true, EpdFontFamily::BOLD);

  // Right side: Clock & Battery in recessed status well
  int rightX = screenWidth - 10;

  // Battery
  const uint16_t percentage = powerManager.getBatteryPercentage();
  char percentText[10];
  snprintf(percentText, sizeof(percentText), "%u%%", static_cast<unsigned>(percentage > 100 ? 100 : percentage));

  const int battIconW = 18;
  const int battIconH = 11;
  const int battIconY = (barHeight - battIconH) / 2;
  rightX -= battIconW;
  const int battIconX = rightX;

  renderer.drawRect(battIconX, battIconY, battIconW - 2, battIconH, true);
  renderer.fillRect(battIconX + battIconW - 2, battIconY + 3, 2, battIconH - 6, true);
  const int fillW = std::max(0, ((battIconW - 4) * static_cast<int>(percentage)) / 100);
  if (fillW > 0) {
    renderer.fillRect(battIconX + 1, battIconY + 1, fillW, battIconH - 2, true);
  }

  const int percentW = renderer.getTextWidth(SMALL_FONT_ID, percentText);
  rightX -= (percentW + 6);
  renderer.drawText(SMALL_FONT_ID, rightX, (barHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2, percentText, true);

  // Clock
  char clockText[16] = {0};
  if (halClock.isAvailable() && halClock.formatTime(clockText, sizeof(clockText), SETTINGS.clockFormat == 1)) {
    rightX -= 8;
    renderer.drawLine(rightX, 4, rightX, barHeight - 5, 1, true);
    rightX -= 8;

    const int clockW = renderer.getTextWidth(SMALL_FONT_ID, clockText);
    rightX -= clockW;
    renderer.drawText(SMALL_FONT_ID, rightX, (barHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2, clockText, true);
  }
}

void NextStepTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                        int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                        bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const bool hasBook = !recentBooks.empty();
  const int sideMargin = 14;
  const Rect winRect{rect.x + sideMargin, rect.y, rect.width - 2 * sideMargin, rect.height};
  const bool isFocused = hasBook && selectorIndex == 0;

  // The title bar follows focus even when the book content comes from cache.
  drawNeXTWindowFrame(renderer, winRect, "Digital Librarian", isFocused);

  const int contentX = winRect.x + 12;
  const int contentY = winRect.y + 28;
  const int contentH = winRect.height - 36;

  if (hasBook && !bufferRestored) {
    const auto& book = recentBooks[0];

    // Left side: Book Cover inside a Sunken NeXT 3D Well
    const int thumbW = 135;
    const int thumbH = contentH;
    const int thumbX = contentX;
    const int thumbY = contentY;

    drawNeXTBevel(renderer, thumbX - 2, thumbY - 2, thumbW + 4, thumbH + 4, true);

    if (!recentBooks[0].coverBmpPath.empty() && !coverRendered) {
      const std::string coverBmpPath =
          UITheme::getCoverThumbPath(recentBooks[0].coverBmpPath, NextStepMetrics::values.homeCoverHeight);
      HalFile file;
      bool hasCover = false;
      if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          hasCover = true;
          int coverW = bitmap.getWidth();
          int coverH = bitmap.getHeight();
          float aspectRatio = static_cast<float>(coverW) / static_cast<float>(coverH);
          int drawH = thumbH;
          int drawW = static_cast<int>(drawH * aspectRatio);
          if (drawW > thumbW) {
            drawW = thumbW;
            drawH = static_cast<int>(drawW / aspectRatio);
          }

          int imgX = thumbX + (thumbW - drawW) / 2;
          int imgY = thumbY + (thumbH - drawH) / 2;

          renderer.drawBitmap(bitmap, imgX, imgY, drawW, drawH);
        }
      }

      if (!hasCover) {
        renderer.drawRect(thumbX, thumbY, thumbW, thumbH, true);
        renderer.drawIcon(BookIcon, thumbX + (thumbW - 32) / 2, thumbY + (thumbH - 32) / 2, 32);
      }
    } else if (!bufferRestored && !coverRendered) {
      renderer.drawRect(thumbX, thumbY, thumbW, thumbH, true);
      renderer.drawIcon(BookIcon, thumbX + (thumbW - 32) / 2, thumbY + (thumbH - 32) / 2, 32);
    }

    // Right side: Metadata, Author, NeXT Reading Gauge
    const int infoX = thumbX + thumbW + 18;
    const int infoW = winRect.x + winRect.width - infoX - 14;
    int curY = thumbY + 6;

    // Book Title
    const std::string truncatedTitle =
        renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), infoW, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, infoX, curY, truncatedTitle.c_str(), true, EpdFontFamily::BOLD);
    curY += renderer.getLineHeight(UI_12_FONT_ID) + 6;

    // Author
    if (!book.author.empty()) {
      const std::string truncatedAuthor =
          renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), infoW, EpdFontFamily::REGULAR);
      renderer.drawText(UI_10_FONT_ID, infoX, curY, truncatedAuthor.c_str(), true, EpdFontFamily::REGULAR);
      curY += renderer.getLineHeight(UI_10_FONT_ID) + 16;
    } else {
      curY += 20;
    }

    // Live Reading Progress from RecentBooksStore
    const int bookProgress = RecentBooksStore::readProgressFromDisk(book);

    // NeXTSTEP Progress Gauge (Sunken bevel with fine dither fill)
    const int barW = std::min(infoW, 160);
    const int barH = 16;
    drawNeXTBevel(renderer, infoX, curY, barW, barH, true);

    const int fillW = ((barW - 4) * std::clamp(bookProgress, 0, 100)) / 100;
    if (fillW > 0) {
      renderer.fillRect(infoX + 2, curY + 2, fillW, barH - 4, true);
    }

    char progText[32];
    snprintf(progText, sizeof(progText), "%d%% completed", bookProgress);
    renderer.drawText(SMALL_FONT_ID, infoX, curY + barH + 6, progText, true, EpdFontFamily::REGULAR);

    if (!coverRendered) {
      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;
    }
  } else if (!hasBook) {
    if (!bufferRestored && !coverRendered) {
      const int textW = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_NO_OPEN_BOOK));
      const int textX = winRect.x + (winRect.width - textW) / 2;
      const int textY = winRect.y + (winRect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
      renderer.drawText(UI_12_FONT_ID, textX, textY, tr(STR_NO_OPEN_BOOK), true);
    }
  }

  // Active window focus ring
  if (isFocused) {
    renderer.drawRect(winRect.x - 2, winRect.y - 2, winRect.width + 4, winRect.height + 4, true);
  }
}

int NextStepTheme::getMenuRowHeight(const GfxRenderer& renderer) const {
  return renderer.getLineHeight(UI_12_FONT_ID) + 12;
}

ButtonMenuLayout NextStepTheme::getButtonMenuLayout(const GfxRenderer& renderer, Rect rect, int buttonCount,
                                                    int selectedIndex) const {
  return ButtonMenuLayout::window(rect, 14, getMenuRowHeight(renderer), buttonCount, selectedIndex);
}

void NextStepTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                   const std::function<std::string(int index)>& buttonLabel,
                                   const std::function<UIIcon(int index)>& rowIcon) const {
  const int sideMargin = 14;
  const Rect winRect{rect.x + sideMargin, rect.y, rect.width - 2 * sideMargin, rect.height};

  // Draw NeXT window frame for Workspace Applications
  drawNeXTWindowFrame(renderer, winRect, "Workspace Applications", selectedIndex >= 0);

  const auto layout = getButtonMenuLayout(renderer, rect, buttonCount, selectedIndex);
  const int innerX = layout.bounds.x;
  const int innerW = layout.bounds.width;
  const int rowHeight = layout.rowHeight;
  drawMenuScrollIndicator(renderer, layout, buttonCount);

  for (int row = 0; row < layout.visibleCount; ++row) {
    const int i = layout.firstVisible + row;
    const int rowY = layout.bounds.y + row * rowHeight;

    const bool selected = (selectedIndex == i);

    if (selected) {
      // Solid dark highlight bar
      renderer.fillRect(innerX, rowY, innerW, rowHeight, true);
    } else {
      // 1px subtle divider between items
      if (i < buttonCount - 1 && selectedIndex != i + 1) {
        renderer.drawLine(innerX + 4, rowY + rowHeight - 1, innerX + innerW - 4, rowY + rowHeight - 1, 1, true);
      }
    }

    // 32px Icon
    const int iconSize = 32;
    const int iconX = innerX + 8;
    const int iconY = rowY + (rowHeight - iconSize) / 2;
    const UIIcon icon = rowIcon(i);
    const uint8_t* iconBitmap = iconForName(icon);

    if (iconBitmap) {
      drawIconColored(renderer, iconBitmap, iconX, iconY, iconSize, !selected);
    }

    // Label
    const int textX = iconBitmap ? iconX + iconSize + 10 : innerX + 12;
    const int textY = rowY + (rowHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    std::string label = buttonLabel(i);
    const int labelWidth = std::max(0, innerX + innerW - textX - 12);
    if (renderer.getTextWidth(UI_12_FONT_ID, label.c_str()) > labelWidth) {
      label = renderer.truncatedText(UI_12_FONT_ID, label.c_str(), labelWidth);
    }

    renderer.drawText(UI_12_FONT_ID, textX, textY, label.c_str(), !selected, EpdFontFamily::REGULAR);
  }
}

void NextStepTheme::drawDockTile(const GfxRenderer& renderer, int x, int y, int w, int h, const char* label,
                                 int actionType, bool active) const {
  // 1. NeXT 3D Beveled Dock Tile
  drawNeXTBevel(renderer, x, y, w, h, active);

  if (active) {
    // Depressed tile: light dither background
    renderer.fillRectDither(x + 2, y + 2, w - 4, h - 4, Color::LightGray);
  }

  // 2. Action Icon on the left side of the Dock tile
  const int iconAreaX = x + 8;
  const int iconAreaY = y + (h - 14) / 2;
  const bool inkColor = true;

  switch (actionType) {
    case 0: {
      // Action 0: Back / Return Chevron Arrow [←]
      renderer.drawLine(iconAreaX + 1, iconAreaY + 7, iconAreaX + 11, iconAreaY + 7, 2, inkColor);
      renderer.drawLine(iconAreaX + 1, iconAreaY + 7, iconAreaX + 6, iconAreaY + 2, 2, inkColor);
      renderer.drawLine(iconAreaX + 1, iconAreaY + 7, iconAreaX + 6, iconAreaY + 12, 2, inkColor);
      break;
    }
    case 1: {
      // Action 1: Select / Confirm Checkmark [✓]
      renderer.drawLine(iconAreaX + 1, iconAreaY + 7, iconAreaX + 4, iconAreaY + 11, 2, inkColor);
      renderer.drawLine(iconAreaX + 4, iconAreaY + 11, iconAreaX + 11, iconAreaY + 3, 2, inkColor);
      break;
    }
    case 2: {
      // Action 2: Up / Previous Arrow [▲]
      for (int r = 0; r < 5; r++) {
        renderer.drawLine(iconAreaX + 6 - r, iconAreaY + 4 + r, iconAreaX + 6 + r, iconAreaY + 4 + r, 1, inkColor);
      }
      break;
    }
    case 3: {
      // Action 3: Down / Next Arrow [▼]
      for (int r = 0; r < 5; r++) {
        renderer.drawLine(iconAreaX + 6 - r, iconAreaY + 9 - r, iconAreaX + 6 + r, iconAreaY + 9 - r, 1, inkColor);
      }
      break;
    }
    default:
      break;
  }

  // 3. Label on the right of the icon
  if (label && label[0] != '\0') {
    const int textStartX = iconAreaX + 16;
    const int maxTextW = x + w - textStartX - 4;
    int font = UI_10_FONT_ID;
    if (renderer.getTextWidth(font, label) > maxTextW) {
      font = SMALL_FONT_ID;
    }
    const int textW = renderer.getTextWidth(font, label);
    const int textX = textStartX + std::max(0, (maxTextW - textW) / 2) + (active ? 1 : 0);
    const int textY = y + (h - renderer.getLineHeight(font)) / 2 + (active ? 1 : 0);
    renderer.drawText(font, textX, textY, label, true, active ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
}

void NextStepTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                    const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }
  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  const int pageWidth = renderer.getScreenWidth();
  constexpr int dockHeight = 44;
  const int dockY = pageHeight - dockHeight;

  // Dock Shelf base: 1px top highlight and boundary
  renderer.fillRect(0, dockY, pageWidth, dockHeight, false);
  renderer.drawLine(0, dockY, pageWidth - 1, dockY, 1, false);
  renderer.drawLine(0, dockY - 1, pageWidth - 1, dockY - 1, 1, true);

  // 4 NeXTSTEP Dock Tiles corresponding to the 4 hardware buttons
  const char* labels[] = {btn1, btn2, btn3, btn4};
  constexpr int gap = 6;
  constexpr int sidePadding = 8;
  const int availW = pageWidth - 2 * sidePadding;
  const int tileW = (availW - 3 * gap) / 4;
  constexpr int tileH = 34;
  const int tileY = dockY + (dockHeight - tileH) / 2;

  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') {
      continue;
    }

    const int tileX = sidePadding + i * (tileW + gap);
    const bool isActive = (i == 1);  // Button 1 (Select/Confirm) is depressed

    drawDockTile(renderer, tileX, tileY, tileW, tileH, labels[i], i, isActive);
  }

  renderer.setOrientation(orig_orientation);
}

void NextStepTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                                  const char* rightLabel) const {
  // Raised 3D subheader band
  drawNeXTBevel(renderer, rect.x, rect.y, rect.width, rect.height, false);

  if (label) {
    const int textY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    renderer.drawText(UI_12_FONT_ID, rect.x + 10, textY, label, true, EpdFontFamily::BOLD);
  }

  if (rightLabel) {
    const int textW = renderer.getTextWidth(SMALL_FONT_ID, rightLabel);
    const int textX = rect.x + rect.width - textW - 10;
    const int textY = rect.y + (rect.height - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
    renderer.drawText(SMALL_FONT_ID, textX, textY, rightLabel, true, EpdFontFamily::REGULAR);
  }
}

void NextStepTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  BaseTheme::fillBatteryIcon(renderer, rect, percentage);
}
