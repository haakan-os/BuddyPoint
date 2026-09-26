#include "MarkdownChecklistActivity.h"

#include "components/UITheme.h"

void MarkdownChecklistActivity::onEnter() {
  failed = !checklist.load(path.c_str());
  UiListActivity::onEnter();
}
void MarkdownChecklistActivity::provideRow(void* context, uint16_t index, freeink::ui::ListItem& row) {
  const auto* self = static_cast<MarkdownChecklistActivity*>(context);
  if (index >= self->checklist.count) return;
  row.label = self->checklist.tasks[index].label;
  row.actionValue = static_cast<int16_t>(index);
}
void MarkdownChecklistActivity::buildScreen(UiScreen& screen) {
  namespace fui = freeink::ui;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
                                      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height),
                                      static_cast<int16_t>(safe.x)});
  fui::ListProps props;
  props.count = listCount();
  props.rowProvider = provideRow;
  props.rowProviderCtx = this;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
void MarkdownChecklistActivity::activateIndex(int index) {
  if (index < 0 || index >= listCount()) return;
  {
    RenderLock lock(*this);
    failed = !checklist.toggle(path.c_str(), index);
  }
  app.clearTapFlash();
  requestUpdate();
}
void MarkdownChecklistActivity::drawFooter() {
  if (failed)
    GUI.drawPopup(renderer, tr(STR_CHECKLIST_SAVE_FAILED));
  else if (checklist.count == 0)
    GUI.drawPopup(renderer, tr(STR_CHECKLIST_EMPTY));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
