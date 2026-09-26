#include "book_sync_server.h"
#include <SD.h>
#include <ArduinoJson.h>
#include "../hal/power_mgr.h"
#include "kosync_client.h"

BookSyncServer SyncServer;

static File uploadFile;

BookSyncServer::BookSyncServer() : _server(HAAKANPOINT_HTTP_PORT), _running(false) {}

bool BookSyncServer::start() {
    if (WiFi.status() != WL_CONNECTED) return false;
    
    // Start mDNS responder for discovery (http://haakanpoint.local)
    if (MDNS.begin(HAAKANPOINT_MDNS_HOST)) {
        MDNS.addService("http", "tcp", HAAKANPOINT_HTTP_PORT);
        MDNS.addServiceTxt("http", "tcp", "device", "xteink-x3");
        MDNS.addServiceTxt("http", "tcp", "version", HAAKANPOINT_SYNC_VERSION);
        Serial.println("[MDNS] Responder started as haakanpoint.local");
    }
    
    // Route registrations
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    _server.on("/api/sync", HTTP_POST, [this]() { handleSync(); });
    
    // File upload endpoint with streaming handler
    _server.on("/api/upload", HTTP_POST, 
        [this]() {
            _server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"File received\"}");
        },
        [this]() {
            handleUpload();
        }
    );
    
    _server.onNotFound([this]() { handleNotFound(); });
    
    _server.begin();
    _running = true;
    Serial.printf("[HTTP] Book sync server active on port %d\n", HAAKANPOINT_HTTP_PORT);
    return true;
}

void BookSyncServer::stop() {
    if (_running) {
        _server.stop();
        MDNS.end();
        _running = false;
        Serial.println("[HTTP] Book sync server stopped");
    }
}

void BookSyncServer::handleClient() {
    if (_running) {
        _server.handleClient();
    }
}

void BookSyncServer::handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>HaakanPoint X3</title><style>body{font-family:sans-serif;padding:20px;text-align:center;}";
    html += ".card{border:1px solid #ccc;border-radius:8px;padding:16px;max-width:400px;margin:auto;}</style></head>";
    html += "<body><div class='card'><h2>HaakanPoint X3</h2>";
    html += "<p>Paired On-The-Go Companion for KOReader</p>";
    html += "<p>Battery: " + String(Power.getBatteryPercentage()) + "%</p>";
    html += "<form method='POST' action='/api/upload' enctype='multipart/form-data'>";
    html += "<input type='file' name='file'><br><br>";
    html += "<input type='submit' value='Send Book to X3'></form></div></body></html>";
    _server.send(200, "text/html", html);
}

void BookSyncServer::handleStatus() {
    StaticJsonDocument<512> doc;
    doc["device"] = HAAKANPOINT_DEVICE_NAME;
    doc["version"] = HAAKANPOINT_SYNC_VERSION;
    doc["battery_pct"] = Power.getBatteryPercentage();
    doc["battery_volts"] = Power.getBatteryVoltage();
    doc["charging"] = Power.isCharging();
    doc["ip"] = WiFi.localIP().toString();
    doc["mdns"] = String(HAAKANPOINT_MDNS_HOST) + ".local";
    
    // Check storage
    uint64_t totalBytes = SD.totalBytes();
    uint64_t usedBytes = SD.usedBytes();
    doc["sd_total_mb"] = (uint32_t)(totalBytes / (1024 * 1024));
    doc["sd_free_mb"] = (uint32_t)((totalBytes - usedBytes) / (1024 * 1024));
    
    String response;
    serializeJson(doc, response);
    _server.send(200, "application/json", response);
}

void BookSyncServer::handleUpload() {
    HTTPUpload& upload = _server.upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        if (!filename.startsWith("/")) {
            filename = "/" + filename;
        }
        
        // Ensure /books directory exists
        if (!SD.exists(PATH_BOOKS_DIR)) {
            SD.mkdir(PATH_BOOKS_DIR);
        }
        
        String targetPath = String(PATH_BOOKS_DIR) + filename;
        Serial.printf("[UPLOAD] Starting upload to %s\n", targetPath.c_str());
        
        if (SD.exists(targetPath)) {
            SD.remove(targetPath);
        }
        
        uploadFile = SD.open(targetPath, FILE_WRITE);
        Power.resetSleepTimer();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) {
            uploadFile.write(upload.buf, upload.currentSize);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
            uploadFile.close();
            Serial.printf("[UPLOAD] Successfully received %s (%u bytes)\n", upload.filename.c_str(), upload.totalSize);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (uploadFile) {
            uploadFile.close();
        }
        Serial.println("[UPLOAD] Upload was aborted");
    }
}

void BookSyncServer::handleSync() {
    // Force immediate background KOSync processing
    bool success = KOSync.processOfflineQueue();
    _server.send(200, "application/json", success ? "{\"status\":\"synced\"}" : "{\"status\":\"idle\"}");
}

void BookSyncServer::handleNotFound() {
    _server.send(404, "application/json", "{\"error\":\"Not Found\"}");
}
