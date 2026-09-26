#include "kosync_client.h"
#include <SD.h>

KOSyncClient KOSync;

KOSyncClient::KOSyncClient() {}

void KOSyncClient::setSettings(const SyncSettings& settings) {
    _settings = settings;
}

bool KOSyncClient::connectWifi(uint32_t timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    if (strlen(_settings.wifiSsid) == 0) return false;
    
    Serial.printf("[WIFI] Connecting to %s...\n", _settings.wifiSsid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(_settings.wifiSsid, _settings.wifiPassword);
    
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(100);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }
    
    Serial.println("[WIFI] Connection timed out");
    return false;
}

void KOSyncClient::disconnectWifi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

bool KOSyncClient::isWifiConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

String KOSyncClient::getApiUrl(const String& path) {
    String base = String(_settings.kosyncServerUrl);
    if (base.endsWith("/")) base.remove(base.length() - 1);
    if (path.startsWith("/")) return base + path;
    return base + "/" + path;
}

void KOSyncClient::setupClient(HTTPClient& http, const String& url) {
    http.setReuse(false);
    http.setTimeout(_settings.syncTimeoutMs);
    http.addHeader("Accept", "application/vnd.koreader.v1+json");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent", "HaakanPoint-X3/1.0 (ESP32-C3)");
    
    if (strlen(_settings.kosyncUsername) > 0 && strlen(_settings.kosyncPasswordMd5) > 0) {
        http.setAuthorization(_settings.kosyncUsername, _settings.kosyncPasswordMd5);
    }
}

SyncResult KOSyncClient::registerUser(const char* username, const char* passwordMd5) {
    if (!connectWifi()) return SyncResult::NO_WIFI;
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); // Allow self-hosted TLS certificates
    
    String url = getApiUrl("/users/create");
    http.begin(client, url);
    setupClient(http, url);
    
    StaticJsonDocument<256> doc;
    doc["username"] = username;
    doc["password"] = passwordMd5;
    
    String reqBody;
    serializeJson(doc, reqBody);
    
    int httpCode = http.POST(reqBody);
    http.end();
    
    if (httpCode == 201 || httpCode == 200) return SyncResult::SUCCESS;
    if (httpCode == 401 || httpCode == 402 || httpCode == 409) return SyncResult::AUTH_FAILED;
    return SyncResult::SERVER_ERROR;
}

SyncResult KOSyncClient::pullProgress(const char* documentHash, ReadingProgress& outProgress) {
    if (!documentHash || strlen(documentHash) == 0) return SyncResult::NOT_FOUND;
    if (!connectWifi()) return SyncResult::NO_WIFI;
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    
    String url = getApiUrl(String("/users/progress/") + documentHash);
    http.begin(client, url);
    setupClient(http, url);
    
    int httpCode = http.GET();
    
    if (httpCode == 200) {
        String payload = http.getString();
        StaticJsonDocument<512> doc;
        DeserializationError err = deserializeJson(doc, payload);
        http.end();
        
        if (!err) {
            strncpy(outProgress.documentHash, documentHash, sizeof(outProgress.documentHash) - 1);
            outProgress.percentage = doc["percentage"] | (doc["progress"].as<float>() * 100.0f);
            outProgress.timestamp = doc["timestamp"] | 0;
            
            if (doc.containsKey("progress_detail")) {
                JsonObject detail = doc["progress_detail"];
                outProgress.spineIndex = detail["spine"] | 0;
                const char* xp = detail["xpath"] | "";
                strncpy(outProgress.xpath, xp, sizeof(outProgress.xpath) - 1);
            }
            
            const char* dev = doc["device"] | "koreader";
            strncpy(outProgress.device, dev, sizeof(outProgress.device) - 1);
            
            Serial.printf("[KOSYNC] Pulled progress for %s: %.1f%%\n", documentHash, outProgress.percentage);
            return SyncResult::SUCCESS;
        }
    } else if (httpCode == 404) {
        http.end();
        return SyncResult::NOT_FOUND;
    } else if (httpCode == 401) {
        http.end();
        return SyncResult::AUTH_FAILED;
    }
    
    http.end();
    return SyncResult::SERVER_ERROR;
}

SyncResult KOSyncClient::pushProgress(const ReadingProgress& progress) {
    if (strlen(progress.documentHash) == 0) return SyncResult::NOT_FOUND;
    
    if (!connectWifi()) {
        queueOfflineSync(progress);
        return SyncResult::NO_WIFI;
    }
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    
    String url = getApiUrl("/users/progress");
    http.begin(client, url);
    setupClient(http, url);
    
    StaticJsonDocument<512> doc;
    doc["document"] = progress.documentHash;
    doc["progress"] = progress.percentage / 100.0f;
    doc["percentage"] = progress.percentage;
    doc["timestamp"] = (int64_t)time(nullptr);
    doc["device"] = HAAKANPOINT_DEVICE_NAME;
    doc["device_id"] = _settings.deviceKey;
    
    JsonObject detail = doc.createNestedObject("progress_detail");
    detail["spine"] = progress.spineIndex;
    detail["page"] = progress.currentPage;
    detail["total_pages"] = progress.totalPages;
    detail["xpath"] = progress.xpath;
    
    String reqBody;
    serializeJson(doc, reqBody);
    
    int httpCode = http.PUT(reqBody);
    http.end();
    
    if (httpCode == 200 || httpCode == 201) {
        Serial.printf("[KOSYNC] Successfully synced %s (%.1f%%)\n", progress.documentHash, progress.percentage);
        return SyncResult::SUCCESS;
    }
    
    // Save to offline queue if upload failed
    queueOfflineSync(progress);
    return SyncResult::SERVER_ERROR;
}

void KOSyncClient::queueOfflineSync(const ReadingProgress& progress) {
    Serial.println("[KOSYNC] Queuing progress for offline sync...");
    // Persist to SD card sync queue file
    File f = SD.open(PATH_SYNC_QUEUE_FILE, FILE_WRITE);
    if (f) {
        StaticJsonDocument<256> doc;
        doc["hash"] = progress.documentHash;
        doc["pct"] = progress.percentage;
        doc["spine"] = progress.spineIndex;
        doc["page"] = progress.currentPage;
        doc["xpath"] = progress.xpath;
        doc["ts"] = progress.timestamp;
        serializeJson(doc, f);
        f.close();
    }
}

bool KOSyncClient::processOfflineQueue() {
    if (!SD.exists(PATH_SYNC_QUEUE_FILE)) return true;
    if (!connectWifi()) return false;
    
    File f = SD.open(PATH_SYNC_QUEUE_FILE, FILE_READ);
    if (!f) return false;
    
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    
    if (!err) {
        ReadingProgress p;
        strncpy(p.documentHash, doc["hash"] | "", sizeof(p.documentHash) - 1);
        p.percentage = doc["pct"] | 0.0f;
        p.spineIndex = doc["spine"] | 0;
        p.currentPage = doc["page"] | 1;
        p.timestamp = doc["ts"] | 0;
        const char* xp = doc["xpath"] | "";
        strncpy(p.xpath, xp, sizeof(p.xpath) - 1);
        
        SyncResult res = pushProgress(p);
        if (res == SyncResult::SUCCESS) {
            SD.remove(PATH_SYNC_QUEUE_FILE);
            return true;
        }
    }
    return false;
}
