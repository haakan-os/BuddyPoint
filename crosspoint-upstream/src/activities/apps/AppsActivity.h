#pragma once

#include <I18n.h>

#include "activities/UiListActivity.h"

class AppsActivity final : public UiListActivity {
 public:
  AppsActivity(GfxRenderer& renderer, MappedInputManager& input) : UiListActivity("Apps", renderer, input) {}

 private:
  freeink::ui::ListItem rows[2]{};
  int listCount() const override { return 2; }
  const char* headerTitle() const override { return tr(STR_APPS); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override { onGoHome(HomeMenuItem::APPS); }
};
