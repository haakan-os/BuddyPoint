#include "BuddySyncActivity.h"

#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void BuddySyncActivity::onEnter() {
  Activity::onEnter();
  {
    RenderLock lock(*this);
    // Rebuildable font caches otherwise compete with Wi-Fi for the small X3 heap.
    if (auto* cache = renderer.getFontCacheManager()) cache->releaseSdFontCaches();
    syncLog.clear();
    transferState = {};
    previousWifiSleep = WiFi.getSleep();
    wifiConnected = WiFi.status() == WL_CONNECTED;
    if (!wifiConnected) syncLog.add(tr(STR_BUDDYSYNC_READY), true);
  }
  if (wifiConnected) onWifiConnected();
  requestUpdate();
}

void BuddySyncActivity::onExit() {
  // ActivityManager already holds RenderLock while destroying the activity.
  webServer.reset();  // Removes the upload observer before this activity dies.
  MDNS.end();
  if (disconnectOnExit)
    WiFi.disconnect(true);
  else
    WiFi.setSleep(previousWifiSleep);
  Activity::onExit();
}

void BuddySyncActivity::startWifiAndSync() {
  if (WiFi.status() == WL_CONNECTED) {
    onWifiConnected();
    return;
  }

  // This activity must outlive the Wi-Fi dialog; use the existing activity heap
  // ownership, but return an error instead of aborting when memory is exhausted.
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  {
    RenderLock lock(*this);
    if (!wifi) {
      LOG_ERR("BuddySync", "OOM: Wi-Fi selection");
      syncLog.add(tr(STR_BUDDYSYNC_MEMORY), false);
      requestUpdate();
      return;
    }
    disconnectOnExit = true;
    isSyncing = true;
    syncProgress = 0;
    syncLog.add(tr(STR_CONNECTING), true);
  }
  requestUpdate();
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (!result.isCancelled && WiFi.status() == WL_CONNECTED) {
      onWifiConnected();
    } else {
      RenderLock lock(*this);
      isSyncing = false;
      syncProgress = 0;
      syncLog.add(tr(STR_BUDDYSYNC_WIFI_CANCELLED), false);
      requestUpdate();
    }
  });
}

void BuddySyncActivity::onWifiConnected() {
  RenderLock lock(*this);
  wifiConnected = true;
  isSyncing = false;
  connectedSSID = WiFi.SSID().c_str();
  connectedIP = WiFi.localIP().toString().c_str();
  if (webServer && webServer->isRunning()) {
    syncLog.add(tr(STR_BUDDYSYNC_LISTENING), true);
    requestUpdate();
    return;
  }

  WiFi.setSleep(false);
  MDNS.end();
  MDNS.begin("haakanpoint");
  webServer = makeUniqueNoThrow<CrossPointWebServer>();
  if (webServer) {
    webServer->setUploadObserver(&BuddySyncActivity::uploadChanged, this);
    webServer->begin();
  }
  if (!webServer || !webServer->isRunning()) {
    LOG_ERR("BuddySync", "Could not start transfer server");
    webServer.reset();
    syncLog.add(tr(STR_BUDDYSYNC_SERVER_FAILED), false);
    requestUpdate();
    return;
  }
  transferState = {};
  syncProgress = 0;
  syncLog.add(tr(STR_BUDDYSYNC_LISTENING), true);
  // Reading-position sync happens in the reader. Authenticating here only
  // blocked the transfer UI on an unrelated remote TLS request.
  const bool hasCredentials = KOREADER_STORE.hasCredentials();
  syncLog.add(hasCredentials ? tr(STR_BUDDYSYNC_KOSYNC_CONFIGURED) : tr(STR_BUDDYSYNC_KOSYNC_UNCONFIGURED),
              hasCredentials);
  requestUpdate();
}

void BuddySyncActivity::uploadChanged(void* context) { static_cast<BuddySyncActivity*>(context)->updateTransfer(); }

void BuddySyncActivity::updateTransfer() {
  // Observer runs on the serving/main task, never the render task. Read scalar
  // upload state first; don't allocate a filename copy on every incoming chunk.
  const auto& upload = webServer->upload;
  const auto update = transferState.observe(upload.attemptId, upload.inProgress, upload.success, millis(), upload.size,
                                            upload.totalExpectedSize);
  if (update == BuddySyncTransferState::Update::None) return;
  {
    RenderLock lock(*this);
    isTransferring = upload.inProgress;
    transferFileName = upload.fileName.c_str();
    transferBytes = upload.size;
    transferTotal = upload.totalExpectedSize;
    transferKbps = upload.currentKbps;
    syncProgress = transferState.getPercent();
    char message[160];
    if (update == BuddySyncTransferState::Update::Succeeded) {
      snprintf(message, sizeof(message), tr(STR_BUDDYSYNC_RECEIVED), transferFileName.c_str());
      syncLog.add(message, true);
    } else if (update == BuddySyncTransferState::Update::Failed) {
      snprintf(message, sizeof(message), tr(STR_BUDDYSYNC_FAILED), upload.error.c_str());
      syncLog.add(message, false);
    }
  }
  // Multipart parsing happens inside handleClient; notify the render task now
  // so progress and completion are visible before that call returns.
  requestUpdate(true);
}

void BuddySyncActivity::loop() {
  // Consume this main-loop input snapshot exactly once, before network work.
  // Re-polling input inside the old 200-call loop discarded Select/arrow events.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasSwipe() == MappedInputManager::SwipeDir::Right) {
    finish();
    return;
  }
  bool connect = false;
  {
    RenderLock lock(*this);
    if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::NavPrevious) ||
        mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      selectedButton = 0;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
               mappedInput.wasReleased(MappedInputManager::Button::NavNext) ||
               mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      selectedButton = 1;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedButton == 1) {
        finish();
        return;
      }
      connect = !isSyncing && !isTransferring;
    }
  }
  if (connect) startWifiAndSync();

  if (wifiConnected && WiFi.status() != WL_CONNECTED) {
    webServer.reset();
    MDNS.end();
    RenderLock lock(*this);
    wifiConnected = false;
    isTransferring = false;
    syncProgress = 0;
    connectedIP.clear();
    syncLog.add(tr(STR_BUDDYSYNC_WIFI_LOST), false);
    requestUpdate();
    return;
  }
  // One call lets the main loop process navigation again as soon as the HTTP
  // request finishes. Idle polling no longer swallows short button presses.
  if (webServer && webServer->isRunning()) webServer->handleClient();
}

void BuddySyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderScreen();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void BuddySyncActivity::renderScreen() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // 1. Menu bar header
  GUI.drawHeader(renderer, Rect{0, 0, pageWidth, metrics.headerHeight}, tr(STR_BUDDYSYNC_TITLE), nullptr);

  const int sideMargin = 16;
  const int winWidth = pageWidth - 2 * sideMargin;
  int currentY = metrics.headerHeight + 12;

  // 2. Main BuddySync window
  const int mainWindowHeight = 270;
  const Rect mainRect{sideMargin, currentY, winWidth, mainWindowHeight};
  drawMacWindow(mainRect, tr(STR_BUDDYSYNC_TITLE));

  int contentY = mainRect.y + 30;  // below title bar
  renderer.drawCenteredText(UI_12_FONT_ID, contentY, tr(STR_BUDDYSYNC_PEERS), true, EpdFontFamily::BOLD);
  contentY += renderer.getLineHeight(UI_12_FONT_ID) + 10;

  // Connection or Transfer status
  if (isTransferring) {
    char rxText[128];
    snprintf(rxText, sizeof(rxText), tr(STR_BUDDYSYNC_RECEIVING), transferFileName.c_str());
    rxText[utf8SafeTruncateBuffer(rxText, static_cast<int>(std::strlen(rxText)))] = '\0';
    const auto receiving = renderer.truncatedText(UI_10_FONT_ID, rxText, mainRect.width - 32, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, contentY, receiving.c_str(), true, EpdFontFamily::BOLD);
    contentY += renderer.getLineHeight(UI_10_FONT_ID) + 6;

    char detailBuf[64];
    if (transferTotal > 0) {
      snprintf(detailBuf, sizeof(detailBuf), tr(STR_BUDDYSYNC_TRANSFER_DETAILS), transferBytes / (1024.0f * 1024.0f),
               transferTotal / (1024.0f * 1024.0f), syncProgress, transferKbps);
    } else {
      snprintf(detailBuf, sizeof(detailBuf), tr(STR_BUDDYSYNC_TRANSFER_BYTES), transferBytes / (1024.0f * 1024.0f),
               transferKbps);
    }
    renderer.drawCenteredText(SMALL_FONT_ID, contentY, detailBuf, true, EpdFontFamily::REGULAR);
    contentY += renderer.getLineHeight(SMALL_FONT_ID) + 8;
  } else {
    std::string statusText = tr(STR_BUDDYSYNC_DISCONNECTED);
    if (isSyncing) {
      statusText = tr(STR_CONNECTING);
    } else if (wifiConnected) {
      statusText = std::string(tr(STR_BUDDYSYNC_CONNECTED)) + connectedSSID;
    }
    statusText = renderer.truncatedText(UI_10_FONT_ID, statusText.c_str(), mainRect.width - 32);
    renderer.drawCenteredText(UI_10_FONT_ID, contentY, statusText.c_str(), true, EpdFontFamily::REGULAR);
    contentY += renderer.getLineHeight(UI_10_FONT_ID) + 6;

    if (wifiConnected && !connectedIP.empty()) {
      std::string ipText = connectedIP + " (haakanpoint.local)";
      renderer.drawCenteredText(SMALL_FONT_ID, contentY, ipText.c_str(), true, EpdFontFamily::REGULAR);
      contentY += renderer.getLineHeight(SMALL_FONT_ID) + 8;
    } else {
      contentY += 12;
    }
  }

  // 1px horizontal divider
  renderer.drawLine(mainRect.x + 16, contentY, mainRect.x + mainRect.width - 16, contentY, 1, true);
  contentY += 12;

  // Progress Bar
  const int barW = mainRect.width - 60;
  const Rect progressRect{mainRect.x + 30, contentY, barW, 14};
  drawMacProgressBar(progressRect, syncProgress);
  contentY += 22;

  // Buttons at bottom of window
  const int buttonWidth = 110;
  const int buttonHeight = 32;
  const int buttonY = mainRect.y + mainRect.height - buttonHeight - 12;

  const Rect syncBtnRect{mainRect.x + 30, buttonY, buttonWidth, buttonHeight};
  drawMacButton(syncBtnRect, tr(STR_BUDDYSYNC_SYNC_NOW), true, selectedButton == 0);

  const Rect backBtnRect{mainRect.x + mainRect.width - buttonWidth - 30, buttonY, buttonWidth, buttonHeight};
  drawMacButton(backBtnRect, tr(STR_BACK), false, selectedButton == 1);

  currentY += mainWindowHeight + 12;

  // 3. Sync log window
  const int logWindowHeight = pageHeight - currentY - metrics.buttonHintsHeight - 10;
  const Rect logRect{sideMargin, currentY, winWidth, logWindowHeight};
  drawMacWindow(logRect, tr(STR_BUDDYSYNC_LOG));

  int logY = logRect.y + 30;
  const int logX = logRect.x + 14;
  const int logRowHeight = renderer.getLineHeight(SMALL_FONT_ID) + 6;
  const size_t visibleRows = std::max(0, (logRect.height - 40) / logRowHeight);
  const size_t firstRow = syncLog.size() > visibleRows ? syncLog.size() - visibleRows : 0;
  for (size_t i = firstRow; i < syncLog.size(); ++i) {
    const auto& entry = syncLog[i];
    renderer.drawText(SMALL_FONT_ID, logX, logY, entry.success ? "+" : "!", true);
    const auto line = renderer.truncatedText(SMALL_FONT_ID, entry.text, logRect.width - 44);
    renderer.drawText(SMALL_FONT_ID, logX + 16, logY, line.c_str(), true, EpdFontFamily::REGULAR);
    logY += logRowHeight;
  }

  // 4. Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BuddySyncActivity::drawMacWindow(Rect rect, const char* title) const {
  constexpr int kRadius = 6;

  // Drop shadow
  renderer.drawLine(rect.x + kRadius, rect.y + rect.height, rect.x + rect.width, rect.y + rect.height, 1, true);
  renderer.drawLine(rect.x + rect.width, rect.y + kRadius, rect.x + rect.width, rect.y + rect.height, 1, true);

  // Clear window background
  renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, kRadius, Color::White);

  // Outer rounded 2px border + 1px inner line
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 2, kRadius, true);
  renderer.drawRoundedRect(rect.x + 3, rect.y + 3, rect.width - 6, rect.height - 6, 1, kRadius - 2, true);

  // Pinstripe title bar (20px tall)
  const int titleBarY = rect.y + 4;
  const int titleBarH = 20;
  const int titleBarW = rect.width - 8;
  drawPinstripeBar(rect.x + 4, titleBarY, titleBarW, titleBarH);
  renderer.drawLine(rect.x + 3, titleBarY + titleBarH, rect.x + rect.width - 4, titleBarY + titleBarH, 1, true);

  // Close box
  renderer.drawRect(rect.x + 8, titleBarY + 4, 12, 12, true);
  renderer.fillRect(rect.x + 10, titleBarY + 6, 8, 8, false);

  // Title
  if (title) {
    const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, title, EpdFontFamily::BOLD);
    const int titleX = rect.x + (rect.width - titleWidth) / 2;
    const int titleY = titleBarY + (titleBarH - renderer.getLineHeight(UI_10_FONT_ID)) / 2;

    renderer.fillRect(titleX - 8, titleBarY + 1, titleWidth + 16, titleBarH - 2, false);
    renderer.drawText(UI_10_FONT_ID, titleX, titleY, title, true, EpdFontFamily::BOLD);
  }
}

void BuddySyncActivity::drawPinstripeBar(int x, int y, int w, int h) const {
  renderer.fillRect(x, y, w, h, false);
  for (int lineY = y + 1; lineY < y + h - 1; lineY += 2) {
    renderer.drawLine(x, lineY, x + w - 1, lineY, 1, true);
  }
}

void BuddySyncActivity::drawMacButton(Rect rect, const char* label, bool isDefault, bool isSelected) const {
  constexpr int btnRadius = 6;

  if (isDefault) {
    renderer.drawRoundedRect(rect.x - 3, rect.y - 3, rect.width + 6, rect.height + 6, 2, btnRadius + 2, true);
  }

  if (isSelected) {
    renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, btnRadius, Color::Black);
    const int labelX = rect.x + (rect.width - renderer.getTextWidth(UI_10_FONT_ID, label)) / 2;
    const int labelY = rect.y + (rect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, labelX, labelY, label, false);
  } else {
    renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, btnRadius, true);
    const int labelX = rect.x + (rect.width - renderer.getTextWidth(UI_10_FONT_ID, label)) / 2;
    const int labelY = rect.y + (rect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, labelX, labelY, label, true);
  }
}

void BuddySyncActivity::drawMacProgressBar(Rect rect, int percent) const {
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, true);
  const int fillWidth = std::max(0, std::min(rect.width - 2, ((rect.width - 2) * percent) / 100));
  if (fillWidth > 0) {
    renderer.fillRectDither(rect.x + 1, rect.y + 1, fillWidth, rect.height - 2, Color::DarkGray);
  }
}
