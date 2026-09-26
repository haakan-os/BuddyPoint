#pragma once
#include <I18n.h>
#include <NoteTools.h>

#include "activities/UiListActivity.h"

class NoteToolsActivity final : public UiListActivity {
 public:
  NoteToolsActivity(GfxRenderer& renderer, MappedInputManager& input, std::string path, bool study)
      : UiListActivity("NoteTools", renderer, input), path(std::move(path)), study(study) {}
  void onEnter() override;
  void onExit() override {
    tools.close();
    UiListActivity::onExit();
  }

 private:
  std::string path;
  bool study;
  notes::Tools tools;
  bool failed = false;
  int listCount() const override { return tools.count; }
  const char* headerTitle() const override { return study ? tr(STR_NOTE_STUDY) : tr(STR_NOTE_LINKS); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawFooter() override;
  void onBackButton() override { activityManager.goToReader(path); }
  static void provideRow(void* context, uint16_t index, freeink::ui::ListItem& row);
};
