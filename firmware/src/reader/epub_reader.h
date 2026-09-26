#pragma once

#include <Arduino.h>
#include <vector>
#include "config.h"

struct PageLocation {
    uint32_t spineIndex;
    uint32_t byteOffset;
    uint32_t charCount;
};

class EpubReader {
public:
    EpubReader();
    
    bool openBook(const String& filepath);
    void closeBook();
    bool isBookOpen() const { return _isOpen; }
    
    // Page navigation
    bool nextPage();
    bool prevPage();
    bool jumpToPercentage(float percentage);
    bool jumpToSpineAndOffset(uint32_t spine, uint32_t offset);
    
    // Rendering
    void renderCurrentPage();
    
    // Progress information
    const ReadingProgress& getProgress() const { return _progress; }
    const String& getBookTitle() const { return _bookTitle; }
    uint32_t getCurrentPage() const { return _progress.currentPage; }
    uint32_t getTotalPages() const { return _progress.totalPages; }
    float getPercentage() const { return _progress.percentage; }

private:
    bool _isOpen;
    String _filepath;
    String _bookTitle;
    ReadingProgress _progress;
    std::vector<PageLocation> _pages;
    
    void paginateDocument();
    void renderHeaderFooter();
    void formatBionicWord(const String& word, int16_t& x, int16_t y);
};

extern EpubReader Reader;
