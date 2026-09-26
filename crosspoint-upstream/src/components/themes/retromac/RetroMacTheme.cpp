#include "RetroMacTheme.h"

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

// 10x10 retro diamond / apple symbol for the top-left menu bar
const uint8_t RetroMenuIcon[10] = {
    0x18,  // ...##...
    0x3C,  // ..####..
    0x7E,  // .######.
    0xFF,  // ########
    0xFF,  // ########
    0xFF,  // ########
    0xFF,  // ########
    0x7E,  // .######.
    0x3C,  // ..####..
    0x18   // ...##...
};

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

// Custom icon plotter supporting both black (ink) and white (inverted) ink
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

void RetroMacTheme::drawPinstripeBar(const GfxRenderer& renderer, int x, int y, int w, int h) const {
  renderer.fillRect(x, y, w, h, false);
  for (int lineY = y + 1; lineY < y + h - 1; lineY += 2) {
    renderer.drawLine(x, lineY, x + w - 1, lineY, 1, true);
  }
}

void RetroMacTheme::drawMacWindowFrame(const GfxRenderer& renderer, Rect rect, const char* title) const {
  constexpr int kRadius = 6;

  // 1. Classic Mac OS 1px drop shadow on bottom and right
  renderer.drawLine(rect.x + kRadius, rect.y + rect.height, rect.x + rect.width, rect.y + rect.height, 1, true);
  renderer.drawLine(rect.x + rect.width, rect.y + kRadius, rect.x + rect.width, rect.y + rect.height, 1, true);

  // 2. White window background with rounded corners
  renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, kRadius, Color::White);

  // 3. 2px outer rounded black border
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 2, kRadius, true);

  // 4. 1px inner rounded line
  renderer.drawRoundedRect(rect.x + 3, rect.y + 3, rect.width - 6, rect.height - 6, 1, kRadius - 2, true);

  // 5. Pinstripe title bar (20px tall)
  const int titleBarY = rect.y + 4;
  const int titleBarH = 20;
  const int titleBarW = rect.width - 8;
  drawPinstripeBar(renderer, rect.x + 4, titleBarY, titleBarW, titleBarH);

  // Title bar bottom divider
  renderer.drawLine(rect.x + 3, titleBarY + titleBarH, rect.x + rect.width - 4, titleBarY + titleBarH, 1, true);

  // Classic Mac square close box on the left of title bar
  renderer.drawRect(rect.x + 8, titleBarY + 4, 12, 12, true);
  renderer.fillRect(rect.x + 10, titleBarY + 6, 8, 8, false);

  // Centered window title with white clear-out
  if (title && title[0] != '\0') {
    const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, title, EpdFontFamily::BOLD);
    const int titleX = rect.x + (rect.width - titleWidth) / 2;
    const int titleY = titleBarY + (titleBarH - renderer.getLineHeight(UI_10_FONT_ID)) / 2;

    renderer.fillRect(titleX - 8, titleBarY + 1, titleWidth + 16, titleBarH - 2, false);
    renderer.drawText(UI_10_FONT_ID, titleX, titleY, title, true, EpdFontFamily::BOLD);
  }
}

void RetroMacTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  const int screenWidth = renderer.getScreenWidth();
  const int barHeight = 28;

  // Classic Mac OS full-width white menu bar with a 1px solid bottom line
  renderer.fillRect(0, 0, screenWidth, barHeight, false);
  renderer.drawLine(0, barHeight - 1, screenWidth - 1, barHeight - 1, 1, true);

  // 1. Left side: retro logo + brand title
  int curX = 10;
  const int iconY = (barHeight - 10) / 2;
  for (int r = 0; r < 10; r++) {
    for (int c = 0; c < 8; c++) {
      if ((RetroMenuIcon[r] >> (7 - c)) & 1) {
        renderer.drawPixel(curX + c, iconY + r, true);
      }
    }
  }
  curX += 16;

  const int textY = (barHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  renderer.drawText(UI_10_FONT_ID, curX, textY, "HaakanPoint", true, EpdFontFamily::BOLD);

  // 2. Right side: Battery & Clock
  int rightX = screenWidth - 12;

  // Clock
  char clockText[16] = {0};
  if (halClock.isAvailable() && halClock.formatTime(clockText, sizeof(clockText), SETTINGS.clockFormat == 1)) {
    const int clockW = renderer.getTextWidth(SMALL_FONT_ID, clockText);
    rightX -= clockW;
    renderer.drawText(SMALL_FONT_ID, rightX, (barHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2, clockText, true);

    // Divider between clock and battery
    rightX -= 10;
    renderer.drawLine(rightX, 5, rightX, barHeight - 6, 1, true);
    rightX -= 10;
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

  // Battery outline
  renderer.drawRect(battIconX, battIconY, battIconW - 2, battIconH, true);
  renderer.fillRect(battIconX + battIconW - 2, battIconY + 3, 2, battIconH - 6, true);  // nub
  const int fillW = std::max(0, ((battIconW - 4) * static_cast<int>(percentage)) / 100);
  if (fillW > 0) {
    renderer.fillRect(battIconX + 1, battIconY + 1, fillW, battIconH - 2, true);
  }

  // Battery percent text
  const int percentW = renderer.getTextWidth(SMALL_FONT_ID, percentText);
  rightX -= (percentW + 6);
  renderer.drawText(SMALL_FONT_ID, rightX, (barHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2, percentText, true);
}

void RetroMacTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                        int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                        bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const bool hasBook = !recentBooks.empty();
  const int sideMargin = 16;
  const Rect winRect{rect.x + sideMargin, rect.y, rect.width - 2 * sideMargin, rect.height};

  if (!bufferRestored && !coverRendered) {
    drawMacWindowFrame(renderer, winRect, tr(STR_CONTINUE_READING));
  }

  const int contentX = winRect.x + 14;
  const int contentY = winRect.y + 30;
  const int contentH = winRect.height - 38;

  if (hasBook && !bufferRestored) {
    const auto& book = recentBooks[0];

    // Left side: Book Cover Thumbnail (larger & prominent)
    const int thumbW = 135;
    const int thumbH = contentH;
    const int thumbX = contentX;
    const int thumbY = contentY;

    if (!recentBooks[0].coverBmpPath.empty() && !coverRendered) {
      const std::string coverBmpPath =
          UITheme::getCoverThumbPath(recentBooks[0].coverBmpPath, RetroMacMetrics::values.homeCoverHeight);
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
          renderer.drawRect(imgX - 1, imgY - 1, drawW + 2, drawH + 2, true);  // 1px picture border
        }
      }

      if (!hasCover) {
        // Retro placeholder book frame
        renderer.drawRect(thumbX, thumbY, thumbW, thumbH, true);
        renderer.drawLine(thumbX + 8, thumbY, thumbX + 8, thumbY + thumbH - 1, 1, true);  // spine
        renderer.drawIcon(BookIcon, thumbX + (thumbW - 32) / 2 + 4, thumbY + (thumbH - 32) / 2, 32);
      }
    } else if (!bufferRestored && !coverRendered) {
      // Placeholder if no cover path
      renderer.drawRect(thumbX, thumbY, thumbW, thumbH, true);
      renderer.drawLine(thumbX + 8, thumbY, thumbX + 8, thumbY + thumbH - 1, 1, true);
      renderer.drawIcon(BookIcon, thumbX + (thumbW - 32) / 2 + 4, thumbY + (thumbH - 32) / 2, 32);
    }

    // Right side: Metadata, Author, Progress
    const int infoX = thumbX + thumbW + 18;
    const int infoW = winRect.x + winRect.width - infoX - 16;
    int curY = thumbY + 4;

    // Title (bold, wrapped or truncated)
    const std::string truncatedTitle =
        renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), infoW, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, infoX, curY, truncatedTitle.c_str(), true, EpdFontFamily::BOLD);
    curY += renderer.getLineHeight(UI_12_FONT_ID) + 6;

    // Author (regular)
    if (!book.author.empty()) {
      const std::string truncatedAuthor =
          renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), infoW, EpdFontFamily::REGULAR);
      renderer.drawText(UI_10_FONT_ID, infoX, curY, truncatedAuthor.c_str(), true, EpdFontFamily::REGULAR);
      curY += renderer.getLineHeight(UI_10_FONT_ID) + 16;
    } else {
      curY += 20;
    }

    // Classic Mac Progress Bar
    const int bookProgress = RecentBooksStore::readProgressFromDisk(book);
    const int barW = std::min(infoW, 160);
    const int barH = 14;
    renderer.drawRect(infoX, curY, barW, barH, true);

    // Progress fill with dithered crosshatch pattern
    const int fillW = ((barW - 2) * std::clamp(bookProgress, 0, 100)) / 100;
    if (fillW > 0) {
      renderer.fillRectDither(infoX + 1, curY + 1, fillW, barH - 2, Color::DarkGray);
    }

    // Progress label
    char progText[32];
    snprintf(progText, sizeof(progText), "%d%% complete", bookProgress);
    renderer.drawText(SMALL_FONT_ID, infoX, curY + barH + 6, progText, true, EpdFontFamily::REGULAR);

    if (!coverRendered) {
      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;
    }
  } else if (!hasBook) {
    // Empty state
    if (!bufferRestored && !coverRendered) {
      const int textW = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_NO_OPEN_BOOK));
      const int textX = winRect.x + (winRect.width - textW) / 2;
      const int textY = winRect.y + (winRect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
      renderer.drawText(UI_12_FONT_ID, textX, textY, tr(STR_NO_OPEN_BOOK), true);
    }
  }

  // Active selection highlight if the book card is focused
  if (hasBook && selectorIndex == 0) {
    renderer.drawRoundedRect(winRect.x - 2, winRect.y - 2, winRect.width + 4, winRect.height + 4, 1, 8, true);
  }
}

int RetroMacTheme::getMenuRowHeight(const GfxRenderer& renderer) const {
  return renderer.getLineHeight(UI_12_FONT_ID) + 12;  // ~36px row height
}

ButtonMenuLayout RetroMacTheme::getButtonMenuLayout(const GfxRenderer& renderer, Rect rect, int buttonCount,
                                                    int selectedIndex) const {
  return ButtonMenuLayout::window(rect, 16, getMenuRowHeight(renderer), buttonCount, selectedIndex);
}

void RetroMacTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                   const std::function<std::string(int index)>& buttonLabel,
                                   const std::function<UIIcon(int index)>& rowIcon) const {
  const int sideMargin = 16;
  const Rect winRect{rect.x + sideMargin, rect.y, rect.width - 2 * sideMargin, rect.height};

  // Draw Mac window frame for "Quick Actions"
  drawMacWindowFrame(renderer, winRect, "Quick Actions");

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
      // Inverted solid black bar for selected row
      renderer.fillRect(innerX, rowY, innerW, rowHeight, true);
    }

    // Draw row divider line for non-selected items
    if (i < buttonCount - 1 && !selected && (selectedIndex != i + 1)) {
      renderer.drawLine(innerX + 6, rowY + rowHeight - 1, innerX + innerW - 6, rowY + rowHeight - 1, 1, true);
    }

    // Icon (24px or 32px)
    const int iconSize = 32;
    const int iconX = innerX + 10;
    const int iconY = rowY + (rowHeight - iconSize) / 2;
    const UIIcon icon = rowIcon(i);
    const uint8_t* iconBitmap = iconForName(icon);

    if (iconBitmap) {
      // Draw white icon if selected, black icon if unselected
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

    // Draw white text if selected, black text if unselected
    renderer.drawText(UI_12_FONT_ID, textX, textY, label.c_str(), !selected, EpdFontFamily::REGULAR);
  }
}

void RetroMacTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                    const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }
  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  const int pageWidth = renderer.getScreenWidth();
  constexpr int barHeight = 34;
  const int barY = pageHeight - barHeight;

  // Clear bottom bar
  renderer.fillRect(0, barY, pageWidth, barHeight, false);

  // Double hairline top border (classic Mac OS footer rule)
  renderer.drawLine(0, barY, pageWidth - 1, barY, 1, true);
  renderer.drawLine(0, barY + 2, pageWidth - 1, barY + 2, 1, true);

  const int btnW = pageWidth >= 528 ? 110 : 100;
  const int gap = pageWidth >= 528 ? 10 : 8;
  const int totalW = 4 * btnW + 3 * gap;
  const int startX = (pageWidth - totalW) / 2;
  constexpr int btnH = 22;
  const int btnY = barY + 6;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') {
      continue;
    }

    const int btnX = startX + i * (btnW + gap);

    // 1. Subtle drop shadow
    renderer.drawLine(btnX + 3, btnY + btnH, btnX + btnW, btnY + btnH, 1, true);
    renderer.drawLine(btnX + btnW, btnY + 3, btnX + btnW, btnY + btnH, 1, true);

    // 2. Clear button background
    renderer.fillRoundedRect(btnX, btnY, btnW, btnH, 4, Color::White);

    // 3. 1px rounded button outline
    renderer.drawRoundedRect(btnX, btnY, btnW, btnH, 1, 4, true);

    // 4. If default/select button (i == 1), draw classic Mac double-border default ring
    if (i == 1) {
      renderer.drawRoundedRect(btnX - 2, btnY - 2, btnW + 4, btnH + 4, 1, 6, true);
    }

    // 5. Centered button text
    int font = UI_10_FONT_ID;
    if (renderer.getTextWidth(font, labels[i]) > btnW - 14) {
      font = SMALL_FONT_ID;
    }
    const int textW = renderer.getTextWidth(font, labels[i]);
    const int textH = renderer.getLineHeight(font);
    const int textX = btnX + (btnW - textW) / 2;
    const int textY = btnY + (btnH - textH) / 2;
    renderer.drawText(font, textX, textY, labels[i], true, (i == 1) ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  renderer.setOrientation(orig_orientation);
}

void RetroMacTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                                  const char* rightLabel) const {
  drawPinstripeBar(renderer, rect.x, rect.y, rect.width, rect.height);

  if (label) {
    const int textW = renderer.getTextWidth(UI_12_FONT_ID, label);
    const int textX = rect.x + (rect.width - textW) / 2;
    const int textY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;

    renderer.fillRect(textX - 6, rect.y + 2, textW + 12, rect.height - 4, false);
    renderer.drawText(UI_12_FONT_ID, textX, textY, label, true, EpdFontFamily::BOLD);
  }

  if (rightLabel) {
    const int textW = renderer.getTextWidth(SMALL_FONT_ID, rightLabel);
    const int textX = rect.x + rect.width - textW - 10;
    const int textY = rect.y + (rect.height - renderer.getLineHeight(SMALL_FONT_ID)) / 2;

    renderer.fillRect(textX - 6, rect.y + 2, textW + 12, rect.height - 4, false);
    renderer.drawText(SMALL_FONT_ID, textX, textY, rightLabel, true, EpdFontFamily::REGULAR);
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, 1, true);
}

void RetroMacTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  BaseTheme::fillBatteryIcon(renderer, rect, percentage);
}
