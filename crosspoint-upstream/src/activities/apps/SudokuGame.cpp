#include "SudokuGame.h"

#include <algorithm>
#include <cstring>

namespace {
// Keep seed ordering and the shuffle algorithm stable: saves store the seed.
constexpr char PUZZLES[][82] = {
    "530070000600195000098000060800060003400803001700020006060000280000419005000080079",
    "000260701680070090190004500820100040004602900050003028009300074040050036703018000",
    "003020600900305001001806400008102900700000008006708200002609500800203009005010300",
};
uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}
void shuffle(uint8_t* values, int count, uint32_t& state) {
  for (int i = count - 1; i > 0; --i) std::swap(values[i], values[nextRandom(state) % (i + 1)]);
}
void permuteAxis(uint8_t* axis, uint32_t& state) {
  uint8_t groups[3] = {0, 1, 2};
  shuffle(groups, 3, state);
  for (int group = 0; group < 3; ++group) {
    uint8_t members[3] = {0, 1, 2};
    shuffle(members, 3, state);
    for (int i = 0; i < 3; ++i) axis[group * 3 + i] = groups[group] * 3 + members[i];
  }
}
void write32(uint8_t* out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (i * 8));
}
uint32_t read32(const uint8_t* in) {
  uint32_t value = 0;
  for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(in[i]) << (i * 8);
  return value;
}
uint32_t checksum(const uint8_t* bytes, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
}  // namespace

void SudokuGame::start(uint32_t seed) {
  puzzleSeed = seed;
  uint32_t state = seed ? seed : 0x9e3779b9u;
  const auto& puzzle = PUZZLES[nextRandom(state) % (sizeof(PUZZLES) / sizeof(PUZZLES[0]))];
  uint8_t digits[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  uint8_t rows[9], columns[9];
  shuffle(digits, 9, state);
  permuteAxis(rows, state);
  permuteAxis(columns, state);
  const bool transpose = (nextRandom(state) & 1) != 0;
  for (int r = 0; r < 9; ++r) {
    for (int c = 0; c < 9; ++c) {
      const int source = transpose ? columns[c] * 9 + rows[r] : rows[r] * 9 + columns[c];
      const int value = puzzle[source] - '0';
      clues[r * 9 + c] = cells[r * 9 + c] = value ? digits[value - 1] : 0;
    }
  }
  selected = 0;
  while (selected < CELL_COUNT - 1 && clues[selected]) ++selected;
}

bool SudokuGame::setCell(int index, uint8_t value) {
  if (index < 0 || index >= CELL_COUNT || value > 9 || clues[index] || cells[index] == value) return false;
  cells[index] = value;
  return true;
}

bool SudokuGame::hasConflict(int index) const {
  if (index < 0 || index >= CELL_COUNT || !cells[index]) return false;
  const int row = index / 9, col = index % 9;
  for (int i = 0; i < CELL_COUNT; ++i) {
    if (i == index || cells[i] != cells[index]) continue;
    if (i / 9 == row || i % 9 == col || (i / 27 == row / 3 && (i % 9) / 3 == col / 3)) return true;
  }
  return false;
}

bool SudokuGame::isComplete() const {
  for (int i = 0; i < CELL_COUNT; ++i) {
    if (!cells[i] || hasConflict(i)) return false;
  }
  return true;
}

void SudokuGame::move(int dx, int dy) {
  const int row = (selected / 9 + dy % 9 + 9) % 9;
  const int col = (selected % 9 + dx % 9 + 9) % 9;
  selected = row * 9 + col;
}

void SudokuGame::encode(uint8_t* bytes, uint32_t sequence) const {
  std::memcpy(bytes, "SDK1", 4);
  write32(bytes + 4, puzzleSeed);
  write32(bytes + 8, sequence);
  std::memcpy(bytes + 12, cells, CELL_COUNT);
  bytes[93] = selected;
  write32(bytes + 94, checksum(bytes, 94));
}

bool SudokuGame::decode(const uint8_t* bytes, size_t size, uint32_t& sequence) {
  if (size != SAVE_SIZE || std::memcmp(bytes, "SDK1", 4) != 0 || read32(bytes + 94) != checksum(bytes, 94) ||
      bytes[93] >= CELL_COUNT)
    return false;
  SudokuGame candidate;
  candidate.start(read32(bytes + 4));
  for (int i = 0; i < CELL_COUNT; ++i) {
    const uint8_t value = bytes[12 + i];
    if (value > 9 || (candidate.clues[i] && candidate.clues[i] != value)) return false;
    candidate.cells[i] = value;
  }
  candidate.selected = bytes[93];
  *this = candidate;
  sequence = read32(bytes + 8);
  return true;
}
