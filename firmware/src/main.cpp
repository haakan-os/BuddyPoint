#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include "config.h"
#include "device_pins.h"
#include "hal/display_driver.h"
#include "hal/power_mgr.h"
#include "reader/book_cache.h"
#include "reader/epub_reader.h"
#include "sync/kosync_client.h"
#include "sync/book_sync_server.h"
#include "ui/ui_manager.h"

AppConfig globalConfig;

void setupButtons() {
    pinMode(PIN_BTN_PREV, INPUT_PULLUP);
    pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
    pinMode(PIN_BTN_HOME, INPUT_PULLUP);
}

void setup() {
    Serial.begin(115200);
    delay(1000); // Allow USB CDC serial to stabilize
    Serial.println("\n=================================");
    Serial.println("   HaakanPoint for Xteink X3     ");
    Serial.println("   KOReader Companion Firmware   ");
    Serial.println("=================================");

    // Initialize Power & Battery Manager
    Power.init();
    Power.resetSleepTimer();

    // Setup input buttons
    setupButtons();

    // Enable power rail for SD card and peripherals
    pinMode(PIN_SD_PWR, OUTPUT);
    digitalWrite(PIN_SD_PWR, HIGH);

    // Initialize Shared SPI Bus (SCK=8, MISO=7, MOSI=10)
    Serial.println("[MAIN] Initializing SPI bus...");
    SPI.begin(PIN_EPD_SCK, PIN_SD_MISO, PIN_EPD_MOSI, -1);

    // Initialize E-ink Display
    Serial.println("[MAIN] Initializing Display...");
    if (!Display.init()) {
        Serial.println("[MAIN] Display init failed!");
    }

    // Initialize MicroSD Storage
    Serial.println("[MAIN] Initializing MicroSD card...");
    if (!Books.init()) {
        Serial.println("[MAIN] SD Card not detected - continuing with memory cache");
    }

    // Load persistent config
    Books.loadConfig(globalConfig);
    KOSync.setSettings(globalConfig.sync);

    // If Wi-Fi credentials are set, attempt connection in background
    if (strlen(globalConfig.sync.wifiSsid) > 0) {
        if (KOSync.connectWifi(3000)) {
            SyncServer.start();
            // Process any pending offline sync items
            KOSync.processOfflineQueue();
        }
    }

    // Initialize UI
    Serial.println("[MAIN] Rendering UI...");
    UI.init();
    UI.render();
    Power.resetSleepTimer();
    Serial.println("[MAIN] HaakanPoint system ready!");
}

void loop() {
    // Handle embedded HTTP book receiver & Web server
    SyncServer.handleClient();

    // Button input polling with debounce
    static unsigned long lastBtnCheck = 0;
    if (millis() - lastBtnCheck > 80) {
        lastBtnCheck = millis();

        if (digitalRead(PIN_BTN_PREV) == LOW) {
            UI.handleButtonPress(PIN_BTN_PREV);
            while (digitalRead(PIN_BTN_PREV) == LOW) delay(20);
        } else if (digitalRead(PIN_BTN_NEXT) == LOW) {
            UI.handleButtonPress(PIN_BTN_NEXT);
            while (digitalRead(PIN_BTN_NEXT) == LOW) delay(20);
        } else if (digitalRead(PIN_BTN_HOME) == LOW) {
            UI.handleButtonPress(PIN_BTN_HOME);
            while (digitalRead(PIN_BTN_HOME) == LOW) delay(20);
        }
    }

    // Check Auto-Sleep / Inactivity
    if (Power.shouldAutoSleep(globalConfig.autoSleepTimeoutSec)) {
        Serial.println("[MAIN] Inactivity timeout reached. Entering sleep...");
        
        // Auto-sync reading progress before sleeping if enabled
        if (globalConfig.sync.autoSyncOnSleep && Reader.isBookOpen()) {
            UI.showSyncNotification("Syncing on sleep...");
            KOSync.pushProgress(Reader.getProgress());
        }

        // Display custom lockscreen
        UI.renderSleepScreen();
        Display.powerOff();
        SyncServer.stop();
        KOSync.disconnectWifi();

        // Enter deep sleep until button press
        Power.enterDeepSleep();
    }

    delay(10);
}
