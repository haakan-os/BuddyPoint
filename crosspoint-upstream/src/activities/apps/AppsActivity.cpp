#include "AppsActivity.h"

#include <Memory.h>

#include "SudokuActivity.h"
#include "activities/home/FileBrowserActivity.h"
#include "components/UITheme.h"

void AppsActivity::buildScreen(UiScreen& screen) {
  namespace fui = freeink::ui;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
                                      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  rows[0].label = tr(STR_SUDOKU);
  rows[0].actionValue = 0;
  rows[1].label = tr(STR_MARKDOWN_VIEWER);
  rows[1].actionValue = 1;
  fui::ListProps props;
  props.items = rows;
  props.count = listCount();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

void AppsActivity::activateIndex(int index) {
  if (index < 0 || index >= listCount()) return;
  // Activities outlive this call and are owned by the manager; no per-move allocations.
  std::unique_ptr<Activity> activity;
  if (index == 0)
    activity = makeUniqueNoThrow<SudokuActivity>(renderer, mappedInput);
  else
    activity = makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, "/", FileBrowserActivity::Mode::Markdown);
  if (!activity) {
    LOG_ERR("APPS", "OOM: app activity");
    return;
  }
  app.clearTapFlash();
  activityManager.replaceActivity(std::move(activity));
}
