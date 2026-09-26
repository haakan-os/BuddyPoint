#pragma once

#include <WiFi.h>

#include <memory>
#include <string>

#include "BuddySyncState.h"
#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "network/CrossPointWebServer.h"

class BuddySyncActivity final : public Activity {
  BuddySyncLog syncLog;
  BuddySyncTransferState transferState;
  int selectedButton = 0;  // 0 = Sync Now, 1 = Back
  bool isSyncing = false;
  int syncProgress = 0;
  bool wifiConnected = false;
  std::string connectedSSID;
  std::string connectedIP;
  std::unique_ptr<CrossPointWebServer> webServer;

  // Live book transfer tracking
  bool isTransferring = false;
  std::string transferFileName;
  size_t transferBytes = 0;
  size_t transferTotal = 0;
  float transferKbps = 0.0f;
  bool disconnectOnExit = false;
  wifi_ps_type_t previousWifiSleep = WIFI_PS_MIN_MODEM;

  void renderScreen() const;
  void drawMacWindow(Rect rect, const char* title) const;
  void drawPinstripeBar(int x, int y, int w, int h) const;
  void drawMacButton(Rect rect, const char* label, bool isDefault, bool isSelected) const;
  void drawMacProgressBar(Rect rect, int percent) const;

  void startWifiAndSync();
  void onWifiConnected();
  void updateTransfer();
  static void uploadChanged(void* context);

 public:
  explicit BuddySyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BuddySync", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return isSyncing || (webServer && webServer->isRunning()); }
  bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
};
