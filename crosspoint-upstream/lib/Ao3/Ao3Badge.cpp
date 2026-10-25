#include "Ao3Badge.h"

namespace ao3 {
void Badge::pixel(int x, int y, bool ink) {
  if (x < 0 || x >= SIZE || y < 0 || y >= SIZE) return;
  auto& byte = pixels[y * (SIZE / 8) + x / 8];
  const auto mask = static_cast<uint8_t>(0x80 >> (x % 8));
  if (ink)
    byte |= mask;
  else
    byte &= static_cast<uint8_t>(~mask);
}
void Badge::glyph(char symbol, int x, int y, bool ink) {
  // Compact 5x7 glyphs, doubled for the 24px quadrants. These are symbols,
  // not UI labels; the full localized legend is available in fic details.
  const uint8_t* rows = nullptr;
  static constexpr uint8_t G[] = {14, 17, 16, 23, 17, 17, 14};
  static constexpr uint8_t T[] = {31, 4, 4, 4, 4, 4, 4};
  static constexpr uint8_t M[] = {17, 27, 21, 21, 17, 17, 17};
  static constexpr uint8_t E[] = {31, 16, 16, 30, 16, 16, 31};
  static constexpr uint8_t R[] = {30, 17, 17, 30, 20, 18, 17};
  static constexpr uint8_t F[] = {31, 16, 16, 30, 16, 16, 16};
  static constexpr uint8_t UNKNOWN[] = {14, 17, 1, 2, 4, 0, 4};
  static constexpr uint8_t WARNING[] = {4, 4, 4, 4, 4, 0, 4};
  static constexpr uint8_t DASH[] = {0, 0, 0, 31, 0, 0, 0};
  static constexpr uint8_t CHECK[] = {0, 1, 1, 2, 18, 12, 8};
  static constexpr uint8_t CROSS[] = {17, 17, 10, 4, 10, 17, 17};
  switch (symbol) {
    case 'G':
      rows = G;
      break;
    case 'T':
      rows = T;
      break;
    case 'M':
      rows = M;
      break;
    case 'E':
      rows = E;
      break;
    case 'R':
      rows = R;
      break;
    case 'F':
      rows = F;
      break;
    case '!':
      rows = WARNING;
      break;
    case '?':
      rows = UNKNOWN;
      break;
    case 'C':
      rows = CHECK;
      break;
    case 'X':
      rows = CROSS;
      break;
    default:
      rows = DASH;
      break;
  }
  for (int r = 0; r < 7; ++r)
    for (int c = 0; c < 5; ++c) {
      if (!(rows[r] & (1 << (4 - c)))) continue;
      for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) pixel(x + c * 2 + dx, y + r * 2 + dy, ink);
    }
}
void Badge::fill(int x, int y, int density) {
  for (int r = y + 2; r < y + 23; ++r)
    for (int c = x + 2; c < x + 23; ++c)
      if (density == 4 || (c + 2 * r) % 4 < density) pixel(c, r);
}
void Badge::build(const Work& work) {
  pixels.fill(0);
  for (int n = 1; n < SIZE - 1; ++n) {
    pixel(n, 0);
    pixel(n, SIZE - 1);
    pixel(0, n);
    pixel(SIZE - 1, n);
    pixel(24, n);
    pixel(n, 24);
  }
  const char rating = ratingSymbol(work.rating);
  const int density = rating == 'E' ? 4 : rating == 'M' ? 3 : rating == 'T' ? 1 : 0;
  fill(0, 0, density);
  glyph(rating, 7, 5, density < 3);
  if (work.readingStatus == ReadingStatus::Waiting || work.readingStatus == ReadingStatus::NewChapters) {
    for (int y = 0; y < 12; ++y)
      for (int x = -y / 2; x <= y / 2; ++x) pixel(36 + x, 6 + y);
    if (work.readingStatus == ReadingStatus::NewChapters)
      for (int y = 2; y < 7; ++y)
        for (int x = 41; x < 46; ++x) pixel(x, y);
  } else {
    const bool finished = work.readingStatus == ReadingStatus::Finished;
    fill(24, 0, finished ? 4 : work.readingStatus == ReadingStatus::Reading ? 1 : 0);
    glyph(finished ? 'F' : work.readingStatus == ReadingStatus::Reading ? 'R' : '-', 31, 5, !finished);
  }
  const char warning = warningSymbol(work.warnings);
  fill(0, 24, warning == '!' ? 4 : warning == 'B' ? 3 : 0);
  glyph(warning == 'B' ? '?' : warning, 7, 29, warning != '!' && warning != 'B');
  fill(24, 24, work.chapters && !work.complete() ? 4 : 0);
  glyph(work.chapters ? (work.complete() ? 'C' : 'X') : '?', 31, 29, !work.chapters || work.complete());
}
}  // namespace ao3
