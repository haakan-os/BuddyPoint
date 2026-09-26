#include <GfxRenderer.h>

#include "BaseTheme.h"
#include "components/UIScale.h"
#include "components/UITheme.h"

void BaseTheme::drawSudokuBoard(const GfxRenderer& renderer, Rect rect, const uint8_t* clues, const uint8_t* cells,
                                const bool* conflicts, int selected, int draft) {
  const int cell = rect.width / 9;
  if (cell < 1) return;
  const auto fonts = uiScaleSpec();
  const int font = renderer.getTextHeight(fonts.bodyFontId) + 6 <= cell ? fonts.bodyFontId : fonts.smallFontId;
  for (int i = 0; i < 81; ++i) {
    const int x = rect.x + i % 9 * cell;
    const int y = rect.y + i / 9 * cell;
    const bool focused = i == selected;
    const bool editing = focused && draft >= 0;
    if (focused) renderer.fillRect(x + 2, y + 2, cell - 3, cell - 3);
    if (editing) renderer.drawRect(x + 4, y + 4, cell - 7, cell - 7, false);
    const int value = editing ? draft : cells[i];
    const auto style = clues[i] ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    if (value || editing) {
      const char text[2] = {value ? static_cast<char>('0' + value) : '-', '\0'};
      const int textWidth = renderer.getTextWidth(font, text, style);
      renderer.drawText(font, x + (cell - textWidth) / 2, y + (cell - renderer.getTextHeight(font)) / 2, text, !focused,
                        style);
    }
    if (conflicts[i] && !editing)
      renderer.drawLine(x + cell / 4, y + cell - 5, x + cell * 3 / 4, y + cell - 5, 2, !focused);
  }
  for (int i = 0; i <= 9; ++i) {
    const int thickness = i % 3 == 0 ? 3 : 1;
    renderer.fillRect(rect.x + i * cell - (thickness / 2), rect.y, thickness, rect.height + 1);
    renderer.fillRect(rect.x, rect.y + i * cell - (thickness / 2), rect.width + 1, thickness);
  }
}

void BaseTheme::drawWrappedHelpText(const GfxRenderer& renderer, Rect rect, const char* label) {
  UITheme::drawCenteredWrappedText(renderer, rect, uiScaleSpec().smallFontId, label, 3, true, EpdFontFamily::REGULAR,
                                   UITheme::TextVerticalAlignment::TOP);
}
