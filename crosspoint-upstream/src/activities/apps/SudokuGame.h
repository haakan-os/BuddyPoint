#pragma once

#include <cstddef>
#include <cstdint>

// Fixed storage only; puzzle transformations preserve the seeds' unique solutions.
class SudokuGame {
 public:
  static constexpr int CELL_COUNT = 81;
  static constexpr size_t SAVE_SIZE = 98;
  uint8_t clues[CELL_COUNT]{};
  uint8_t cells[CELL_COUNT]{};
  uint8_t selected = 0;

  void start(uint32_t seed);
  bool setCell(int index, uint8_t value);
  bool hasConflict(int index) const;
  bool isComplete() const;
  void move(int dx, int dy);
  void encode(uint8_t* bytes, uint32_t sequence) const;
  bool decode(const uint8_t* bytes, size_t size, uint32_t& sequence);
  static bool isNewer(uint32_t candidate, uint32_t previous) {
    const uint32_t distance = candidate - previous;
    return distance != 0 && distance < 0x80000000u;
  }

 private:
  uint32_t puzzleSeed = 0;
};
