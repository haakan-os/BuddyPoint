#include "book_cache.h"
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <MD5Builder.h>
#include "device_pins.h"

BookCache Books;

BookCache::BookCache() {}

bool BookCache::init() {
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
    
    if (!SD.begin(PIN_SD_CS, SPI, 10000000)) {
        _sdMounted = false;
        Serial.println("[SD] Note: SD card mount skipped (no card or format issue)");
        return false;
    }
    
    _sdMounted = true;
    ensureDirectories();
    Serial.println("[SD] MicroSD mounted successfully");
    return true;
}

void BookCache::ensureDirectories() {
    if (!_sdMounted) return;
    if (!SD.exists(PATH_BOOKS_DIR)) SD.mkdir(PATH_BOOKS_DIR);
    if (!SD.exists(PATH_CACHE_DIR)) SD.mkdir(PATH_CACHE_DIR);
    if (!SD.exists(PATH_FONTS_DIR)) SD.mkdir(PATH_FONTS_DIR);
}

bool BookCache::calculateMD5(const String& filepath, char* outHash) {
    if (!_sdMounted || !SD.exists(filepath)) return false;
    
    File f = SD.open(filepath, FILE_READ);
    if (!f) return false;
    
    MD5Builder md5;
    md5.begin();
    md5.addStream(f, f.size());
    md5.calculate();
    
    String hashStr = md5.toString();
    strncpy(outHash, hashStr.c_str(), 32);
    outHash[32] = '\0';
    f.close();
    return true;
}

void BookCache::scanBooks(std::vector<BookItem>& outBooks) {
    outBooks.clear();
    if (!_sdMounted) return;
    File root = SD.open(PATH_BOOKS_DIR);
    if (!root || !root.isDirectory()) return;
    
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String name = String(file.name());
            if (name.endsWith(".epub") || name.endsWith(".txt")) {
                BookItem item;
                item.filename = name;
                item.fullPath = String(PATH_BOOKS_DIR) + "/" + name;
                item.fileSize = file.size();
                
                // Friendly title without extension
                int dotIdx = name.lastIndexOf('.');
                item.title = (dotIdx > 0) ? name.substring(0, dotIdx) : name;
                item.author = "Unknown Author";
                item.readingProgress = 0.0f;
                item.lastReadTimestamp = 0;
                
                // Calculate or lookup cached MD5
                char hash[65];
                if (calculateMD5(item.fullPath, hash)) {
                    item.md5Hash = String(hash);
                }
                
                outBooks.push_back(item);
            }
        }
        file = root.openNextFile();
    }
}

bool BookCache::loadCurrentState(ReadingProgress& outProgress) {
    if (!_sdMounted || !SD.exists(PATH_CURRENT_BOOK_FILE)) return false;
    
    File f = SD.open(PATH_CURRENT_BOOK_FILE, FILE_READ);
    if (!f) return false;
    
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    
    if (err) return false;
    
    strncpy(outProgress.documentHash, doc["hash"] | "", sizeof(outProgress.documentHash) - 1);
    strncpy(outProgress.bookTitle, doc["title"] | "", sizeof(outProgress.bookTitle) - 1);
    strncpy(outProgress.bookPath, doc["path"] | "", sizeof(outProgress.bookPath) - 1);
    outProgress.percentage = doc["pct"] | 0.0f;
    outProgress.currentPage = doc["page"] | 1;
    outProgress.totalPages = doc["total"] | 1;
    outProgress.spineIndex = doc["spine"] | 0;
    strncpy(outProgress.xpath, doc["xpath"] | "", sizeof(outProgress.xpath) - 1);
    outProgress.timestamp = doc["ts"] | 0;
    
    return true;
}

bool BookCache::saveCurrentState(const ReadingProgress& progress) {
    if (!_sdMounted) return false;
    File f = SD.open(PATH_CURRENT_BOOK_FILE, FILE_WRITE);
    if (!f) return false;
    
    StaticJsonDocument<512> doc;
    doc["hash"] = progress.documentHash;
    doc["title"] = progress.bookTitle;
    doc["path"] = progress.bookPath;
    doc["pct"] = progress.percentage;
    doc["page"] = progress.currentPage;
    doc["total"] = progress.totalPages;
    doc["spine"] = progress.spineIndex;
    doc["xpath"] = progress.xpath;
    doc["ts"] = (int64_t)time(nullptr);
    
    serializeJson(doc, f);
    f.close();
    return true;
}

bool BookCache::loadConfig(AppConfig& config) {
    if (!_sdMounted || !SD.exists(PATH_CONFIG_FILE)) return false;
    
    File f = SD.open(PATH_CONFIG_FILE, FILE_READ);
    if (!f) return false;
    
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    
    if (err) return false;
    
    // Reading settings
    config.reading.fontSize = doc["reading"]["font_size"] | 16;
    config.reading.lineSpacing = doc["reading"]["line_spacing"] | 4;
    config.reading.bionicReading = doc["reading"]["bionic"] | false;
    config.reading.darkMode = doc["reading"]["dark_mode"] | false;
    
    // Sync settings
    strncpy(config.sync.wifiSsid, doc["sync"]["ssid"] | "", sizeof(config.sync.wifiSsid) - 1);
    strncpy(config.sync.wifiPassword, doc["sync"]["password"] | "", sizeof(config.sync.wifiPassword) - 1);
    strncpy(config.sync.kosyncServerUrl, doc["sync"]["server"] | "https://sync.koreader.rocks", sizeof(config.sync.kosyncServerUrl) - 1);
    strncpy(config.sync.kosyncUsername, doc["sync"]["user"] | "", sizeof(config.sync.kosyncUsername) - 1);
    strncpy(config.sync.kosyncPasswordMd5, doc["sync"]["pass_md5"] | "", sizeof(config.sync.kosyncPasswordMd5) - 1);
    config.sync.autoSyncOnOpen = doc["sync"]["auto_open"] | true;
    config.sync.autoSyncOnClose = doc["sync"]["auto_close"] | true;
    config.sync.autoSyncOnSleep = doc["sync"]["auto_sleep"] | true;
    
    config.autoSleepTimeoutSec = doc["auto_sleep_sec"] | 300;
    return true;
}

bool BookCache::saveConfig(const AppConfig& config) {
    File f = SD.open(PATH_CONFIG_FILE, FILE_WRITE);
    if (!f) return false;
    
    StaticJsonDocument<1024> doc;
    JsonObject reading = doc.createNestedObject("reading");
    reading["font_size"] = config.reading.fontSize;
    reading["line_spacing"] = config.reading.lineSpacing;
    reading["bionic"] = config.reading.bionicReading;
    reading["dark_mode"] = config.reading.darkMode;
    
    JsonObject sync = doc.createNestedObject("sync");
    sync["ssid"] = config.sync.wifiSsid;
    sync["password"] = config.sync.wifiPassword;
    sync["server"] = config.sync.kosyncServerUrl;
    sync["user"] = config.sync.kosyncUsername;
    sync["pass_md5"] = config.sync.kosyncPasswordMd5;
    sync["auto_open"] = config.sync.autoSyncOnOpen;
    sync["auto_close"] = config.sync.autoSyncOnClose;
    sync["auto_sleep"] = config.sync.autoSyncOnSleep;
    
    doc["auto_sleep_sec"] = config.autoSleepTimeoutSec;
    
    serializeJson(doc, f);
    f.close();
    return true;
}
