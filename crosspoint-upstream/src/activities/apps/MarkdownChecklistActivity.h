#pragma once
#include <I18n.h>
#include <MarkdownChecklist.h>

#include "activities/UiListActivity.h"

class MarkdownChecklistActivity final : public UiListActivity {
 public:
  MarkdownChecklistActivity(GfxRenderer& renderer, MappedInputManager& input, std::string path)
      : UiListActivity("MarkdownChecklist", renderer, input), path(std::move(path)) {}
  void onEnter() override;

 private:
  std::string path;
  markdown::Checklist checklist;
  bool failed = false;
  int listCount() const override { return static_cast<int>(checklist.count); }
  const char* headerTitle() const override { return checklist.truncated ? tr(STR_CHECKLIST_LIMIT) : tr(STR_CHECKLIST); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawFooter() override;
  void onBackButton() override { activityManager.goToReader(path); }
  static void provideRow(void* context, uint16_t index, freeink::ui::ListItem& row);
};
