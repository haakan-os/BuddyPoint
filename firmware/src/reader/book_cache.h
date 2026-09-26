#pragma once

#include <Arduino.h>
#include <vector>
#include "config.h"

struct BookItem {
    String filename;
    String fullPath;
    String title;
    String author;
    String md5Hash;
    size_t fileSize;
    float readingProgress;
    uint32_t lastReadTimestamp;
};

class BookCache {
public:
    BookCache();
    bool init();
    
    void scanBooks(std::vector<BookItem>& outBooks);
    bool calculateMD5(const String& filepath, char* outHash);
    
    bool loadCurrentState(ReadingProgress& outProgress);
    bool saveCurrentState(const ReadingProgress& progress);
    
    bool loadConfig(AppConfig& config);
    bool saveConfig(const AppConfig& config);
    
    bool isMounted() const { return _sdMounted; }

private:
    bool _sdMounted = false;
    void ensureDirectories();
};

extern BookCache Books;
