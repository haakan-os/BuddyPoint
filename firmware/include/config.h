#pragma once

#include <Arduino.h>

#define HAAKANPOINT_DEVICE_NAME   "HaakanPoint X3"
#define HAAKANPOINT_MDNS_HOST     "haakanpoint"
#define HAAKANPOINT_HTTP_PORT     80
#define HAAKANPOINT_SYNC_VERSION  "1.0.0"

// File system paths on SD Card
#define PATH_BOOKS_DIR            "/books"
#define PATH_CACHE_DIR            "/cache"
#define PATH_FONTS_DIR            "/fonts"
#define PATH_CONFIG_FILE          "/cache/config.json"
#define PATH_SYNC_QUEUE_FILE      "/cache/sync_queue.json"
#define PATH_CURRENT_BOOK_FILE    "/cache/current_reading.json"

// Reading settings
struct ReadingSettings {
    uint8_t fontSize = 16;
    uint8_t lineSpacing = 4;
    uint8_t marginX = 16;
    uint8_t marginY = 24;
    bool bionicReading = false;
    bool darkMode = false;
    uint8_t refreshEveryNPages = 5;
};

// Network & KOSync Settings
struct SyncSettings {
    char wifiSsid[64] = "";
    char wifiPassword[64] = "";
    char kosyncServerUrl[128] = "https://sync.koreader.rocks";
    char kosyncUsername[64] = "";
    char kosyncPasswordMd5[65] = ""; // MD5 hash of password
    char deviceKey[64] = "haakanpoint-x3";
    bool autoSyncOnOpen = true;
    bool autoSyncOnClose = true;
    bool autoSyncOnSleep = true;
    uint32_t syncTimeoutMs = 8000;
};

// Current Reading State Structure (Compatible with KOReader progress)
struct ReadingProgress {
    char documentHash[65] = "";   // MD5 checksum of the book
    char bookTitle[128] = "";
    char bookPath[256] = "";
    float percentage = 0.0f;       // 0.0 to 100.0%
    uint32_t currentPage = 1;
    uint32_t totalPages = 1;
    uint32_t spineIndex = 0;
    char xpath[128] = "";          // KOReader XPath anchor
    int64_t timestamp = 0;         // Epoch timestamp
    char device[32] = "haakanpoint";
};

// Global application configuration
struct AppConfig {
    ReadingSettings reading;
    SyncSettings sync;
    ReadingProgress lastReading;
    uint32_t autoSleepTimeoutSec = 300; // 5 minutes
};
