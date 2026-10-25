#pragma once

#include <Ao3Badge.h>

#include <array>

#include "activities/UiListActivity.h"
#include "network/Ao3Client.h"

class Ao3Activity final : public UiListActivity {
 public:
  Ao3Activity(GfxRenderer& renderer, MappedInputManager& input);
  void onEnter() override;
  void onExit() override;
  bool preventAutoSleep() override;
  bool handleHomeGesture() override;

 private:
  enum class View { Menu, List, Detail, Busy, Message };
  enum class Source { Downloads, Bookmarks, Search };
  enum class Job { None, List, Work, Login, Download, UpdateOne, Updates, Bookmarks };
  Ao3Client client;
  View view = View::Menu;
  Source source = Source::Downloads;
  Job pending = Job::None;
  std::vector<ao3::Work> works;
  ao3::Work selected;
  // A page has at most 20 works plus navigation/actions. Owned labels remain
  // stable while FreeInkUI renders; no allocations occur in buildScreen().
  std::vector<std::string> labels;
  std::array<freeink::ui::ListItem, ao3::PAGE_SIZE + 4> rows{};
  // 5.6 KiB for one bounded page of masks, owned by the heap-allocated activity.
  std::array<ao3::Badge, ao3::PAGE_SIZE> badges{};
  std::vector<std::string> subtitles;
  std::string query;
  std::string loginUser;
  std::string loginPassword;
  std::string message;
  std::string progressText;
  std::string status;
  size_t page = 1;
  bool more = false;
  bool local = false;
  bool cancelled = false;
  bool homeAfterCancel = false;
  bool wifiStarted = false;
  unsigned long lastPaint = 0;
  size_t processed = 0, downloaded = 0, unchanged = 0, failed = 0;

  const char* headerTitle() const override;
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void drawFooter() override;
  bool handleCustomInput() override;
  void show(View next);
  void rebuildLabels();
  void schedule(Job job);
  void execute(Job job);
  bool ensureWifi(Job job);
  void editQuery(bool link);
  void editLogin();
  void showError();
  void showMessage(const std::string& text);
  void sync(bool bookmarks);
  std::string listUrl(size_t number) const;
  static bool progress(void* context, size_t bytes, size_t total);
  static void cancelAction(const freeink::ui::ActionEvent&, void* context);
};
