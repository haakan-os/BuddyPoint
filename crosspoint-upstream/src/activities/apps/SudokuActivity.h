#pragma once

#include "SudokuGame.h"
#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"

class SudokuActivity final : public Activity {
 public:
  SudokuActivity(GfxRenderer& renderer, MappedInputManager& input) : Activity("Sudoku", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode { Board, Edit, Menu, ConfirmNew };
  SudokuGame game;
  Mode mode = Mode::Board;
  uint8_t draft = 0;
  int menuIndex = 0;
  int saveSlot = -1;
  uint32_t saveSequence = 0;
  uint32_t changedAt = 0;
  uint32_t lastSaveAttempt = 0;
  bool dirty = false;
  bool saveFailed = false;
  unsigned int refreshCount = 0;

  void newGame();
  void loadGame();
  bool saveGame();
  void markChanged();
  void activateMenu();
  Rect contentArea() const;
  Rect boardArea() const;
};
