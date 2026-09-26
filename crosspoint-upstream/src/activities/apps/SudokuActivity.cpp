#include "SudokuActivity.h"

#include <Arduino.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <esp_system.h>

#include <algorithm>

#include "components/UIScale.h"
#include "components/UITheme.h"

namespace {
constexpr const char* SAVE_PATHS[] = {"/.crosspoint/sudoku-0.bin", "/.crosspoint/sudoku-1.bin"};
constexpr uint32_t SAVE_DELAY_MS = 2000;
constexpr uint32_t RETRY_DELAY_MS = 10000;

bool readSave(int slot, SudokuGame& game, uint32_t& sequence) {
  if (!Storage.exists(SAVE_PATHS[slot])) return false;
  HalFile file;
  uint8_t bytes[SudokuGame::SAVE_SIZE];
  if (!Storage.openFileForRead("SUDOKU", SAVE_PATHS[slot], file) || file.size() != sizeof(bytes) ||
      file.read(bytes, sizeof(bytes)) != sizeof(bytes))
    return false;
  return game.decode(bytes, sizeof(bytes), sequence);
}
}  // namespace

void SudokuActivity::onEnter() {
  Activity::onEnter();
  loadGame();
  requestUpdate();
}

void SudokuActivity::onExit() {
  saveGame();
  Activity::onExit();
}

void SudokuActivity::markChanged() {
  dirty = true;
  changedAt = millis();
}

void SudokuActivity::newGame() {
  game.start(esp_random());
  mode = Mode::Board;
  markChanged();
}

void SudokuActivity::loadGame() {
  SudokuGame candidate;
  for (int slot = 0; slot < 2; ++slot) {
    uint32_t sequence = 0;
    if (readSave(slot, candidate, sequence) && (saveSlot < 0 || SudokuGame::isNewer(sequence, saveSequence))) {
      game = candidate;
      saveSequence = sequence;
      saveSlot = slot;
    }
  }
  if (saveSlot < 0) newGame();
}

bool SudokuActivity::saveGame() {
  if (!dirty) return true;
  lastSaveAttempt = millis();
  const int nextSlot = saveSlot == 0 ? 1 : 0;
  uint8_t bytes[SudokuGame::SAVE_SIZE];
  game.encode(bytes, saveSequence + 1);
  bool written = false;
  if (Storage.ensureDirectoryExists("/.crosspoint")) {
    HalFile file;
    if (Storage.openFileForWrite("SUDOKU", SAVE_PATHS[nextSlot], file)) {
      written = file.write(bytes, sizeof(bytes)) == sizeof(bytes);
      // Check close's sync result before retiring the previous save slot.
      written = file.close() && written;
    }
  }
  saveFailed = !written;
  if (!written) {
    LOG_ERR("SUDOKU", "Could not save puzzle progress");
    return false;
  }
  saveSlot = nextSlot;
  ++saveSequence;
  dirty = false;
  return true;
}

Rect SudokuActivity::contentArea() const {
  Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int x = std::max(safe.x, left);
  const int y = std::max(safe.y, top);
  const int endX = std::min(safe.x + safe.width, renderer.getScreenWidth() - right);
  const int endY = std::min(safe.y + safe.height, renderer.getScreenHeight() - bottom);
  const auto& metrics = UITheme::getInstance().getMetrics();
  return Rect{x + metrics.contentSidePadding, y + metrics.topPadding + metrics.headerHeight,
              std::max(0, endX - x - 2 * metrics.contentSidePadding),
              std::max(0, endY - y - metrics.topPadding - metrics.headerHeight)};
}

Rect SudokuActivity::boardArea() const {
  const Rect content = contentArea();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int helpHeight = renderer.getTextHeight(uiScaleSpec().smallFontId) * 3 + metrics.verticalSpacing * 3;
  const int side = std::max(0, std::min(content.width - 2, content.height - helpHeight - 2)) / 9 * 9;
  return Rect{content.x + (content.width - side) / 2, content.y + metrics.verticalSpacing, side, side};
}

void SudokuActivity::activateMenu() {
  if (mode == Mode::ConfirmNew) {
    if (menuIndex == 1)
      newGame();
    else
      mode = Mode::Menu;
    menuIndex = 0;
  } else if (menuIndex == 0) {
    mode = Mode::Board;
  } else if (menuIndex == 1) {
    mode = Mode::ConfirmNew;
    menuIndex = 0;
  } else if (saveGame()) {
    activityManager.goToApps();
  }
  requestUpdate();
}

void SudokuActivity::loop() {
  // The render task reads the board and mode together; serialize every mutation.
  RenderLock lock;
  const uint32_t now = millis();
  if (dirty && now - changedAt >= SAVE_DELAY_MS && (!saveFailed || now - lastSaveAttempt >= RETRY_DELAY_MS)) {
    const bool previousFailure = saveFailed;
    saveGame();
    if (saveFailed != previousFailure) requestUpdate();
  }
  using Button = MappedInputManager::Button;
  if (mode == Mode::Menu || mode == Mode::ConfirmNew) {
    const int count = mode == Mode::Menu ? 3 : 2;
    if (mappedInput.wasReleased(Button::Back)) {
      mode = mode == Mode::Menu ? Mode::Board : Mode::Menu;
      menuIndex = 0;
      requestUpdate();
    } else if (mappedInput.wasReleased(Button::Confirm)) {
      activateMenu();
    } else if (mappedInput.wasReleased(Button::NavPrevious) || mappedInput.wasReleased(Button::NavNext)) {
      menuIndex = (menuIndex + (mappedInput.wasReleased(Button::NavPrevious) ? count - 1 : 1)) % count;
      requestUpdate();
    } else {
      const auto layout = GUI.getButtonMenuLayout(renderer, contentArea(), count, menuIndex);
      int row = -1;
      if (mappedInput.rowTouch(row, layout.bounds.y, layout.rowHeight + layout.rowSpacing, layout.visibleCount,
                               layout.bounds.x, layout.bounds.x + layout.bounds.width,
                               layout.rowHeight) == MappedInputManager::RowTouch::Tap) {
        menuIndex = row + layout.firstVisible;
        activateMenu();
      }
    }
    return;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    mode = mode == Mode::Edit ? Mode::Board : Mode::Menu;
    menuIndex = 0;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    if (mode == Mode::Edit) {
      if (game.setCell(game.selected, draft)) markChanged();
      mode = Mode::Board;
    } else if (!game.clues[game.selected]) {
      mode = Mode::Edit;
      draft = game.cells[game.selected];
    }
    requestUpdate();
    return;
  }
  int dx = 0, dy = 0;
  if (mappedInput.wasReleased(Button::ScreenLeft))
    dx = -1;
  else if (mappedInput.wasReleased(Button::ScreenRight))
    dx = 1;
  else if (mappedInput.wasReleased(Button::ScreenUp))
    dy = -1;
  else if (mappedInput.wasReleased(Button::ScreenDown))
    dy = 1;
  if (dx || dy) {
    if (mode == Mode::Edit)
      draft = (draft + dx + dy + 10) % 10;
    else
      game.move(dx, dy);
    requestUpdate();
    return;
  }
  int x, y;
  if (mappedInput.wasScreenTapped(x, y) && mode == Mode::Board) {
    const Rect board = boardArea();
    if (board.width > 0 && x >= board.x && y >= board.y && x < board.x + board.width && y < board.y + board.height) {
      game.selected = (y - board.y) / (board.width / 9) * 9 + (x - board.x) / (board.width / 9);
      requestUpdate();
    }
  }
}

void SudokuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight},
                 mode == Mode::ConfirmNew ? tr(STR_SUDOKU_REPLACE) : tr(STR_SUDOKU));
  if (mode == Mode::Menu || mode == Mode::ConfirmNew) {
    const bool confirm = mode == Mode::ConfirmNew;
    GUI.drawButtonMenu(
        renderer, contentArea(), confirm ? 2 : 3, menuIndex,
        [confirm](int index) -> std::string {
          if (confirm) return index == 0 ? tr(STR_CANCEL) : tr(STR_SUDOKU_NEW);
          if (index == 0) return tr(STR_RESUME);
          return index == 1 ? tr(STR_SUDOKU_NEW) : tr(STR_SUDOKU_SAVE_EXIT);
        },
        [](int) { return UIIcon::None; });
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    if (saveFailed) {
      const Rect content = contentArea();
      GUI.drawWrappedHelpText(
          renderer,
          Rect{content.x, content.y + content.height - metrics.listRowHeight, content.width, metrics.listRowHeight},
          tr(STR_SAVE_PROGRESS_FAILED));
    }
  } else {
    bool conflicts[SudokuGame::CELL_COUNT];
    bool anyConflict = false;
    for (int i = 0; i < SudokuGame::CELL_COUNT; ++i) {
      conflicts[i] = game.hasConflict(i);
      anyConflict |= conflicts[i];
    }
    const Rect board = boardArea();
    GUI.drawSudokuBoard(renderer, board, game.clues, game.cells, conflicts, game.selected,
                        mode == Mode::Edit ? draft : -1);
    const char* status = tr(STR_SUDOKU_HELP);
    if (saveFailed)
      status = tr(STR_SAVE_PROGRESS_FAILED);
    else if (mode == Mode::Edit)
      status = tr(STR_SUDOKU_EDIT_HELP);
    else if (game.isComplete())
      status = tr(STR_SUDOKU_COMPLETE);
    else if (anyConflict)
      status = tr(STR_SUDOKU_CONFLICT);
    else if (game.clues[game.selected])
      status = tr(STR_SUDOKU_FIXED);
    const Rect content = contentArea();
    const int helpY = board.y + board.height + metrics.verticalSpacing;
    GUI.drawWrappedHelpText(renderer, Rect{content.x, helpY, content.width, content.y + content.height - helpY},
                            status);
    const auto labels =
        mode == Mode::Edit
            ? mappedInput.mapDirectionalLabels(tr(STR_CANCEL), tr(STR_DONE), tr(STR_SUDOKU_MINUS), tr(STR_SUDOKU_PLUS),
                                               tr(STR_SUDOKU_MINUS), tr(STR_SUDOKU_PLUS))
            : mappedInput.mapDirectionalLabels(tr(STR_SUDOKU_MENU), tr(STR_SUDOKU_EDIT), tr(STR_DIR_LEFT),
                                               tr(STR_DIR_RIGHT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  // Bound ghosting without forcing a slow full refresh on every cursor move.
  renderer.displayBuffer(refreshCount++ % 12 == 0 ? HalDisplay::RefreshMode::FULL_REFRESH
                                                  : HalDisplay::RefreshMode::FAST_REFRESH);
}
