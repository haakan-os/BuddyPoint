#include "Win95Theme.h"

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

void Win95Theme::draw3DBevel(const GfxRenderer& renderer, int x, int y, int w, int h, bool sunken) const {
  // Outer 1px black border
  renderer.drawRect(x, y, w, h, true);

  if (!sunken) {
    // Raised bevel: White on top/left, dark gray on bottom/right
    renderer.drawLine(x + 1, y + 1, x + w - 2, y + 1, 1, false);         // Top highlight (white)
    renderer.drawLine(x + 1, y + 1, x + 1, y + h - 2, 1, false);         // Left highlight (white)
    renderer.drawLine(x + 1, y + h - 2, x + w - 2, y + h - 2, 1, true);  // Bottom shadow
    renderer.drawLine(x + w - 2, y + 1, x + w - 2, y + h - 2, 1, true);  // Right shadow
  } else {
    // Sunken bevel: Dark on top/left, white on bottom/right
    renderer.drawLine(x + 1, y + 1, x + w - 2, y + 1, 1, true);           // Top shadow
    renderer.drawLine(x + 1, y + 1, x + 1, y + h - 2, 1, true);           // Left shadow
    renderer.drawLine(x + 1, y + h - 2, x + w - 2, y + h - 2, 1, false);  // Bottom highlight
    renderer.drawLine(x + w - 2, y + 1, x + w - 2, y + h - 2, 1, false);  // Right highlight
  }
}

void Win95Theme::drawWin95WindowFrame(const GfxRenderer& renderer, Rect rect, const char* title, bool active) const {
  // 1. Raised 3D window border
  draw3DBevel(renderer, rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4, true);

  // 2. Title bar
  const int tbX = rect.x + 3;
  const int tbY = rect.y + 3;
  const int tbW = rect.width - 6;
  const int tbH = 22;

  if (active) {
    // Solid black active title bar
    renderer.fillRect(tbX, tbY, tbW, tbH, true);
  } else {
    // Inactive title bar: light dithered gray
    renderer.fillRectDither(tbX, tbY, tbW, tbH, Color::LightGray);
  }

  // 3. Title text (left-aligned in Windows 95)
  if (title && title[0] != '\0') {
    const int textY = tbY + (tbH - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, tbX + 6, textY, title, !active, EpdFontFamily::BOLD);
  }

  // 4. Windows 95 min/max/close buttons on the top right
  const int btnSize = 14;
  const int btnY = tbY + 4;

  // Close [X] button
  const int closeX = tbX + tbW - btnSize - 3;
  draw3DBevel(renderer, closeX, btnY, btnSize, btnSize, false);
  renderer.drawLine(closeX + 3, btnY + 3, closeX + 9, btnY + 9, 1, true);
  renderer.drawLine(closeX + 9, btnY + 3, closeX + 3, btnY + 9, 1, true);

  // Maximize [□] button
  const int maxX = closeX - btnSize - 2;
  draw3DBevel(renderer, maxX, btnY, btnSize, btnSize, false);
  renderer.drawRect(maxX + 3, btnY + 3, 7, 7, true);
  renderer.drawLine(maxX + 3, btnY + 4, maxX + 9, btnY + 4, 1, true);

  // Minimize [_] button
  const int minX = maxX - btnSize - 2;
  draw3DBevel(renderer, minX, btnY, btnSize, btnSize, false);
  renderer.drawLine(minX + 3, btnY + 9, minX + 9, btnY + 9, 2, true);
}

void Win95Theme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  const int screenWidth = renderer.getScreenWidth();
  const int barHeight = 26;

  // Header band with 1px bottom border
  renderer.fillRect(0, 0, screenWidth, barHeight, false);
  renderer.drawLine(0, barHeight - 1, screenWidth - 1, barHeight - 1, 1, true);

  // Brand on left
  const int textY = (barHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  renderer.drawText(UI_10_FONT_ID, 12, textY, "HaakanPoint [Win95]", true, EpdFontFamily::BOLD);

  // Right side: Clock & Battery
  int rightX = screenWidth - 12;

  // Clock
  char clockText[16] = {0};
  if (halClock.isAvailable() && halClock.formatTime(clockText, sizeof(clockText), SETTINGS.clockFormat == 1)) {
    const int clockW = renderer.getTextWidth(SMALL_FONT_ID, clockText);
    rightX -= clockW;
    renderer.drawText(SMALL_FONT_ID, rightX, (barHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2, clockText, true);

    rightX -= 8;
    renderer.drawLine(rightX, 4, rightX, barHeight - 5, 1, true);
    rightX -= 8;
  }

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
}

void Win95Theme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                     int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                     bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const bool hasBook = !recentBooks.empty();
  const int sideMargin = 14;
  const Rect winRect{rect.x + sideMargin, rect.y, rect.width - 2 * sideMargin, rect.height};
  const bool isFocused = hasBook && selectorIndex == 0;

  // The title bar follows focus even when the book content comes from cache.
  drawWin95WindowFrame(renderer, winRect, tr(STR_CONTINUE_READING), isFocused);

  const int contentX = winRect.x + 12;
  const int contentY = winRect.y + 28;
  const int contentH = winRect.height - 36;

  if (hasBook && !bufferRestored) {
    const auto& book = recentBooks[0];

    // Left side: Book Cover inside a Sunken 3D Well
    const int thumbW = 135;
    const int thumbH = contentH;
    const int thumbX = contentX;
    const int thumbY = contentY;

    draw3DBevel(renderer, thumbX - 2, thumbY - 2, thumbW + 4, thumbH + 4, true);

    if (!recentBooks[0].coverBmpPath.empty() && !coverRendered) {
      const std::string coverBmpPath =
          UITheme::getCoverThumbPath(recentBooks[0].coverBmpPath, Win95Metrics::values.homeCoverHeight);
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

    // Right side: Metadata, Author, Segmented Progress Bar
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

    // Classic Windows 95 Segmented Progress Bar
    const int bookProgress = RecentBooksStore::readProgressFromDisk(book);
    const int barW = std::min(infoW, 160);
    const int barH = 16;
    draw3DBevel(renderer, infoX, curY, barW, barH, true);  // Sunken 3D bar well

    const int fillW = ((barW - 4) * std::clamp(bookProgress, 0, 100)) / 100;
    // Draw segmented blocks (classic Win95 style: 7px blocks with 2px gap)
    for (int bx = infoX + 3; bx < infoX + 3 + fillW; bx += 9) {
      const int blockW = std::min(7, (infoX + 3 + fillW) - bx);
      if (blockW > 0) {
        renderer.fillRect(bx, curY + 3, blockW, barH - 6, true);
      }
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

int Win95Theme::getMenuRowHeight(const GfxRenderer& renderer) const {
  return renderer.getLineHeight(UI_12_FONT_ID) + 12;  // ~36px row height
}

ButtonMenuLayout Win95Theme::getButtonMenuLayout(const GfxRenderer& renderer, Rect rect, int buttonCount,
                                                 int selectedIndex) const {
  return ButtonMenuLayout::window(rect, 14, getMenuRowHeight(renderer), buttonCount, selectedIndex);
}

void Win95Theme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                const std::function<std::string(int index)>& buttonLabel,
                                const std::function<UIIcon(int index)>& rowIcon) const {
  const int sideMargin = 14;
  const Rect winRect{rect.x + sideMargin, rect.y, rect.width - 2 * sideMargin, rect.height};

  // Draw Windows 95 window frame for Programs / Quick Actions
  drawWin95WindowFrame(renderer, winRect, "Programs", selectedIndex >= 0);

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
      // Solid dark navy/black highlight bar
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

void Win95Theme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                 const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }
  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  const int pageWidth = renderer.getScreenWidth();
  constexpr int barHeight = 36;
  const int barY = pageHeight - barHeight;

  // 1. Taskbar background
  renderer.fillRect(0, barY, pageWidth, barHeight, false);
  renderer.drawLine(0, barY, pageWidth - 1, barY, 1, false);         // Top white highlight
  renderer.drawLine(0, barY - 1, pageWidth - 1, barY - 1, 1, true);  // Top dark line

  constexpr int btnH = 26;
  const int btnY = barY + 5;

  // 2. Start button on far left
  const int startX = 4;
  const int startBtnW = pageWidth >= 528 ? 78 : 72;
  draw3DBevel(renderer, startX, btnY, startBtnW, btnH, false);

  // 4-pane Windows logo
  const int logoX = startX + 5;
  const int logoY = btnY + 8;
  renderer.fillRect(logoX, logoY, 4, 4, true);
  renderer.fillRect(logoX + 5, logoY, 4, 4, true);
  renderer.fillRect(logoX, logoY + 5, 4, 4, true);
  renderer.fillRect(logoX + 5, logoY + 5, 4, 4, true);

  // "Start" text
  const char* startLabel = "Start";
  int startFont = UI_10_FONT_ID;
  if (renderer.getTextWidth(startFont, startLabel) > (startBtnW - 22)) {
    startFont = SMALL_FONT_ID;
  }
  const int textX = logoX + 12;
  const int startTextY = btnY + (btnH - renderer.getLineHeight(startFont)) / 2;
  renderer.drawText(startFont, textX, startTextY, startLabel, true, EpdFontFamily::BOLD);

  // Vertical groove divider after Start button
  const int divX = startX + startBtnW + 4;
  renderer.drawLine(divX, btnY + 2, divX, btnY + btnH - 3, 1, true);
  renderer.drawLine(divX + 1, btnY + 2, divX + 1, btnY + btnH - 3, 1, false);

  // 3. Running App Task Buttons corresponding to hardware buttons
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const int tasksStartX = divX + 6;
  const int availW = pageWidth - tasksStartX - 4;
  const int gap = 4;
  const int btnW = std::max(50, (availW - 3 * gap) / 4);

  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') {
      continue;
    }

    const int currentBtnX = tasksStartX + i * (btnW + gap);
    const bool isActiveTask = (i == 1);  // Primary / Confirm action is depressed

    if (isActiveTask) {
      // Sunken/depressed 3D bevel for active running app
      draw3DBevel(renderer, currentBtnX, btnY, btnW, btnH, true);
      // Dither fill on pressed button
      renderer.fillRectDither(currentBtnX + 2, btnY + 2, btnW - 4, btnH - 4, Color::LightGray);
    } else {
      // Raised 3D bevel for background task
      draw3DBevel(renderer, currentBtnX, btnY, btnW, btnH, false);
    }

    // Label inside taskbar button
    int font = UI_10_FONT_ID;
    if (renderer.getTextWidth(font, labels[i]) > btnW - 12) {
      font = SMALL_FONT_ID;
    }
    const int textW = renderer.getTextWidth(font, labels[i]);
    const int textH = renderer.getLineHeight(font);
    const int textX = currentBtnX + (btnW - textW) / 2 + (isActiveTask ? 1 : 0);
    const int textY = btnY + (btnH - textH) / 2 + (isActiveTask ? 1 : 0);
    renderer.drawText(font, textX, textY, labels[i], true, isActiveTask ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  renderer.setOrientation(orig_orientation);
}

void Win95Theme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                               const char* rightLabel) const {
  // Raised 3D subheader band
  draw3DBevel(renderer, rect.x, rect.y, rect.width, rect.height, false);

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

void Win95Theme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  BaseTheme::fillBatteryIcon(renderer, rect, percentage);
}
