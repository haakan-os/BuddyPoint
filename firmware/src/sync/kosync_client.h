#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"

enum class SyncResult {
    SUCCESS,
    NO_WIFI,
    AUTH_FAILED,
    NOT_FOUND,
    NETWORK_ERROR,
    SERVER_ERROR
};

class KOSyncClient {
public:
    KOSyncClient();
    
    void setSettings(const SyncSettings& settings);
    
    bool connectWifi(uint32_t timeoutMs = 8000);
    void disconnectWifi();
    bool isWifiConnected() const;
    
    // KOSync Auth & Registration
    SyncResult registerUser(const char* username, const char* passwordMd5);
    SyncResult authenticate();
    
    // Two-way reading progress sync
    SyncResult pullProgress(const char* documentHash, ReadingProgress& outProgress);
    SyncResult pushProgress(const ReadingProgress& progress);
    
    // Offline queue management
    void queueOfflineSync(const ReadingProgress& progress);
    bool processOfflineQueue();

private:
    SyncSettings _settings;
    String _authHeader;
    
    String getApiUrl(const String& path);
    void setupClient(HTTPClient& http, const String& url);
};

extern KOSyncClient KOSync;
