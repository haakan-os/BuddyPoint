#include "NoteToolsActivity.h"

#include <FsHelpers.h>
#include <Memory.h>

#include "activities/reader/DictionaryDefinitionActivity.h"
#include "components/UITheme.h"

void NoteToolsActivity::onEnter() {
  failed = !tools.load(path, study ? 2 : 1);
  UiListActivity::onEnter();
}
void NoteToolsActivity::provideRow(void* context, uint16_t index, freeink::ui::ListItem& row) {
  auto* self = static_cast<NoteToolsActivity*>(context);
  if (index >= self->tools.count) return;
  row.label = self->tools.labels[index];
  row.actionValue = index;
}
void NoteToolsActivity::buildScreen(UiScreen& screen) {
  namespace fui = freeink::ui;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  fui::ListProps props;
  props.count = tools.count;
  props.rowProvider = provideRow;
  props.rowProviderCtx = this;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
void NoteToolsActivity::activateIndex(int index) {
  if (index < 0 || index >= listCount()) return;
  std::string value;
  if (!tools.value(index, value)) {
    failed = true;
    requestUpdate();
    return;
  }
  app.clearTapFlash();
  if (study) {
    // The paged answer viewer owns at most 2 KiB of answer text. No second reader stays resident.
    auto activity = makeUniqueNoThrow<DictionaryDefinitionActivity>(renderer, mappedInput, tr(STR_NOTE_STUDY),
                                                                    tools.labels[index], false, std::move(value));
    if (!activity) {
      LOG_ERR("NOTES", "OOM: answer viewer");
      return;
    }
    startActivityForResult(std::move(activity), [](const ActivityResult&) {});
  } else {
    // Restrict navigation to existing Markdown inside the same top-level sync folder.
    const auto rootEnd = path.find('/', 1);
    const auto root = path.substr(0, rootEnd == std::string::npos ? 1 : rootEnd + 1);
    if (value.compare(0, root.size(), root) != 0 || value.find("/../") != std::string::npos ||
        value.find("/./") != std::string::npos || value.find('\\') != std::string::npos ||
        !FsHelpers::hasMarkdownExtension(value) || !Storage.exists(value.c_str())) {
      failed = true;
      requestUpdate();
      return;
    }
    activityManager.goToReader(value);
  }
}
void NoteToolsActivity::drawFooter() {
  if (failed || !tools.count) GUI.drawPopup(renderer, tr(STR_NOTE_TOOLS_EMPTY));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
