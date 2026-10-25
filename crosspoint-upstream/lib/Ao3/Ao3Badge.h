#pragma once
#include <array>

#include "Ao3.h"

namespace ao3 {
// A page owns these small masks; rendering needs no image files or heap allocation.
// BW1: row-major, MSB first, set bits use the selected theme's foreground.
struct Badge {
  static constexpr uint16_t SIZE = 48;
  std::array<uint8_t, SIZE * SIZE / 8> pixels{};
  void build(const Work& work);

 private:
  void pixel(int x, int y, bool ink = true);
  void glyph(char symbol, int x, int y, bool ink = true);
  void fill(int x, int y, int density);
};
}  // namespace ao3
