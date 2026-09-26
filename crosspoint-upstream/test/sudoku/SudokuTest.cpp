#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <set>
#include <string>

#include "activities/apps/SudokuGame.h"

namespace {
// Independent solver: verify uniqueness without relying on the game's conflict checker.
bool allowed(const std::array<uint8_t, 81>& board, int index, uint8_t value) {
  const int r = index / 9, c = index % 9;
  for (int i = 0; i < 9; ++i) {
    if (board[r * 9 + i] == value || board[i * 9 + c] == value ||
        board[(r / 3 * 3 + i / 3) * 9 + c / 3 * 3 + i % 3] == value)
      return false;
  }
  return true;
}
int solve(std::array<uint8_t, 81>& board, std::array<uint8_t, 81>& solution) {
  int best = -1, fewest = 10;
  for (int i = 0; i < 81; ++i) {
    if (board[i]) continue;
    int count = 0;
    for (uint8_t n = 1; n <= 9; ++n) count += allowed(board, i, n);
    if (count == 0) return 0;
    if (count < fewest) {
      best = i;
      fewest = count;
    }
  }
  if (best < 0) {
    solution = board;
    return 1;
  }
  int solutions = 0;
  for (uint8_t n = 1; n <= 9 && solutions < 2; ++n) {
    if (!allowed(board, best, n)) continue;
    board[best] = n;
    solutions += solve(board, solution);
    board[best] = 0;
  }
  return solutions;
}
void rehash(std::array<uint8_t, SudokuGame::SAVE_SIZE>& bytes) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < 94; ++i) hash = (hash ^ bytes[i]) * 16777619u;
  for (int i = 0; i < 4; ++i) bytes[94 + i] = static_cast<uint8_t>(hash >> (i * 8));
}
}  // namespace

TEST(Sudoku, ShuffledPuzzlesHaveOneSolutionAndCanBeCompleted) {
  std::set<std::string> seen;
  for (uint32_t seed = 0; seed < 300; ++seed) {
    SudokuGame game;
    game.start(seed);
    EXPECT_FALSE(game.isComplete());
    std::array<uint8_t, 81> board, solution;
    std::memcpy(board.data(), game.clues, 81);
    seen.emplace(reinterpret_cast<const char*>(game.clues), 81);
    for (int i = 0; i < 81; ++i) EXPECT_FALSE(game.hasConflict(i));
    ASSERT_EQ(solve(board, solution), 1) << seed;
    for (int i = 0; i < 81; ++i) {
      if (!game.clues[i]) EXPECT_TRUE(game.setCell(i, solution[i]));
    }
    EXPECT_TRUE(game.isComplete()) << seed;
  }
  EXPECT_EQ(seen.size(), 300u);
}

TEST(Sudoku, CluesAreProtectedAndEntriesCanBeCleared) {
  SudokuGame game;
  game.start(42);
  for (int i = 0; i < 81; ++i) {
    if (game.clues[i]) {
      EXPECT_FALSE(game.setCell(i, 0));
      EXPECT_FALSE(game.setCell(i, game.clues[i] % 9 + 1));
      EXPECT_EQ(game.cells[i], game.clues[i]);
    } else {
      EXPECT_TRUE(game.setCell(i, 7));
      EXPECT_FALSE(game.setCell(i, 7));
      EXPECT_TRUE(game.setCell(i, 0));
    }
  }
  EXPECT_FALSE(game.setCell(-1, 1));
  EXPECT_FALSE(game.setCell(81, 1));
  EXPECT_FALSE(game.setCell(game.selected, 10));
}

TEST(Sudoku, DetectsRowColumnAndBoxConflictsWithoutFlaggingBlanks) {
  for (const int duplicate : {1, 9, 10}) {
    SudokuGame game;
    game.cells[0] = game.cells[duplicate] = 5;
    EXPECT_TRUE(game.hasConflict(0));
    EXPECT_TRUE(game.hasConflict(duplicate));
    EXPECT_FALSE(game.hasConflict(80));
    EXPECT_FALSE(game.isComplete());
  }
  SudokuGame game;
  game.cells[0] = game.cells[40] = 5;
  EXPECT_FALSE(game.hasConflict(0));
  EXPECT_FALSE(game.hasConflict(40));
}

TEST(Sudoku, NavigationWrapsWithinRowsAndColumns) {
  SudokuGame game;
  game.selected = 0;
  game.move(-1, 0);
  EXPECT_EQ(game.selected, 8);
  game.move(1, 0);
  EXPECT_EQ(game.selected, 0);
  game.move(0, -1);
  EXPECT_EQ(game.selected, 72);
  game.move(0, 1);
  EXPECT_EQ(game.selected, 0);
  game.selected = 80;
  game.move(1, 0);
  EXPECT_EQ(game.selected, 72);
  game.move(0, 1);
  EXPECT_EQ(game.selected, 0);
}

TEST(Sudoku, SaveRoundTripKeepsMistakesCluesAndSelection) {
  SudokuGame game;
  game.start(UINT32_MAX);
  const int index = game.selected;
  game.setCell(index, 8);
  std::array<uint8_t, SudokuGame::SAVE_SIZE> bytes;
  game.encode(bytes.data(), 123);
  SudokuGame loaded;
  uint32_t sequence = 0;
  ASSERT_TRUE(loaded.decode(bytes.data(), bytes.size(), sequence));
  EXPECT_EQ(sequence, 123u);
  EXPECT_EQ(loaded.selected, index);
  EXPECT_EQ(std::memcmp(game.clues, loaded.clues, 81), 0);
  EXPECT_EQ(std::memcmp(game.cells, loaded.cells, 81), 0);
}

TEST(Sudoku, CorruptOrTruncatedSaveNeverReplacesAnExistingGame) {
  SudokuGame game;
  game.start(42);
  std::array<uint8_t, SudokuGame::SAVE_SIZE> bytes;
  game.encode(bytes.data(), 17);
  SudokuGame loaded;
  loaded.start(43);
  const SudokuGame original = loaded;
  uint32_t sequence = 20;
  for (size_t i = 0; i < bytes.size(); ++i) {
    auto damaged = bytes;
    damaged[i] ^= 0x80;
    EXPECT_FALSE(loaded.decode(damaged.data(), damaged.size(), sequence));
    EXPECT_FALSE(loaded.decode(bytes.data(), i, sequence));
  }
  EXPECT_EQ(std::memcmp(loaded.cells, original.cells, 81), 0);
  EXPECT_EQ(sequence, 20u);
}

TEST(Sudoku, RejectsInvalidCursorDigitsAndChangedCluesEvenWithValidChecksum) {
  SudokuGame game;
  game.start(42);
  std::array<uint8_t, SudokuGame::SAVE_SIZE> bytes;
  game.encode(bytes.data(), 17);
  uint32_t sequence = 0;
  auto bad = bytes;
  bad[93] = 81;
  rehash(bad);
  EXPECT_FALSE(game.decode(bad.data(), bad.size(), sequence));
  bad = bytes;
  bad[12 + game.selected] = 10;
  rehash(bad);
  EXPECT_FALSE(game.decode(bad.data(), bad.size(), sequence));
  for (int i = 0; i < 81; ++i) {
    if (!game.clues[i]) continue;
    bad = bytes;
    bad[12 + i] = 0;
    rehash(bad);
    EXPECT_FALSE(game.decode(bad.data(), bad.size(), sequence));
  }
}

TEST(Sudoku, SaveOrderingHandlesSequenceRollover) {
  EXPECT_TRUE(SudokuGame::isNewer(11, 10));
  EXPECT_FALSE(SudokuGame::isNewer(10, 11));
  EXPECT_FALSE(SudokuGame::isNewer(10, 10));
  EXPECT_TRUE(SudokuGame::isNewer(0, UINT32_MAX));
  EXPECT_FALSE(SudokuGame::isNewer(UINT32_MAX, 0));
}
