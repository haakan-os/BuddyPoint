#include "epub_reader.h"
#include <SD.h>
#include "../hal/display_driver.h"
#include "book_cache.h"

EpubReader Reader;

EpubReader::EpubReader() : _isOpen(false) {}

bool EpubReader::openBook(const String& filepath) {
    if (!SD.exists(filepath)) return false;
    
    closeBook();
    _filepath = filepath;
    
    // Extract title
    int slashIdx = filepath.lastIndexOf('/');
    String filename = (slashIdx >= 0) ? filepath.substring(slashIdx + 1) : filepath;
    int dotIdx = filename.lastIndexOf('.');
    _bookTitle = (dotIdx >= 0) ? filename.substring(0, dotIdx) : filename;
    
    strncpy(_progress.bookTitle, _bookTitle.c_str(), sizeof(_progress.bookTitle) - 1);
    strncpy(_progress.bookPath, filepath.c_str(), sizeof(_progress.bookPath) - 1);
    
    // Calculate MD5 for KOSync pairing
    char hash[65];
    if (Books.calculateMD5(filepath, hash)) {
        strncpy(_progress.documentHash, hash, sizeof(_progress.documentHash) - 1);
    }
    
    paginateDocument();
    _isOpen = true;
    return true;
}

void EpubReader::closeBook() {
    if (_isOpen) {
        Books.saveCurrentState(_progress);
        _isOpen = false;
        _pages.clear();
    }
}

void EpubReader::paginateDocument() {
    _pages.clear();
    File f = SD.open(_filepath, FILE_READ);
    if (!f) return;
    
    size_t fileSize = f.size();
    // Estimate page size based on 480x800 screen viewport (~1200 characters per page)
    const uint32_t charsPerPage = 1200;
    uint32_t offset = 0;
    uint32_t spine = 0;
    
    while (offset < fileSize) {
        PageLocation page;
        page.spineIndex = spine;
        page.byteOffset = offset;
        page.charCount = charsPerPage;
        _pages.push_back(page);
        
        offset += charsPerPage;
    }
    f.close();
    
    _progress.totalPages = _pages.empty() ? 1 : _pages.size();
    if (_progress.currentPage == 0) _progress.currentPage = 1;
    if (_progress.currentPage > _progress.totalPages) _progress.currentPage = _progress.totalPages;
    _progress.percentage = ((float)_progress.currentPage / (float)_progress.totalPages) * 100.0f;
}

bool EpubReader::nextPage() {
    if (!_isOpen || _progress.currentPage >= _progress.totalPages) return false;
    _progress.currentPage++;
    _progress.percentage = ((float)_progress.currentPage / (float)_progress.totalPages) * 100.0f;
    return true;
}

bool EpubReader::prevPage() {
    if (!_isOpen || _progress.currentPage <= 1) return false;
    _progress.currentPage--;
    _progress.percentage = ((float)_progress.currentPage / (float)_progress.totalPages) * 100.0f;
    return true;
}

bool EpubReader::jumpToPercentage(float percentage) {
    if (!_isOpen) return false;
    if (percentage < 0.0f) percentage = 0.0f;
    if (percentage > 100.0f) percentage = 100.0f;
    
    uint32_t targetPage = (uint32_t)((percentage / 100.0f) * _progress.totalPages);
    if (targetPage < 1) targetPage = 1;
    if (targetPage > _progress.totalPages) targetPage = _progress.totalPages;
    
    _progress.currentPage = targetPage;
    _progress.percentage = percentage;
    return true;
}

bool EpubReader::jumpToSpineAndOffset(uint32_t spine, uint32_t offset) {
    if (!_isOpen || _pages.empty()) return false;
    
    for (size_t i = 0; i < _pages.size(); i++) {
        if (_pages[i].spineIndex == spine && _pages[i].byteOffset >= offset) {
            _progress.currentPage = i + 1;
            _progress.percentage = ((float)_progress.currentPage / (float)_progress.totalPages) * 100.0f;
            return true;
        }
    }
    return false;
}

void EpubReader::renderHeaderFooter() {
    // Header: Book Title
    Display.renderText(16, 12, _bookTitle.c_str(), 1, false, 0x00);
    Display.drawFastHLine(16, 28, SCREEN_WIDTH - 32, 0x00);
    
    // Footer: Page Number and Percentage
    char footerBuf[64];
    snprintf(footerBuf, sizeof(footerBuf), "%u / %u  (%.1f%%)", _progress.currentPage, _progress.totalPages, _progress.percentage);
    Display.drawFastHLine(16, SCREEN_HEIGHT - 30, SCREEN_WIDTH - 32, 0x00);
    Display.renderText(16, SCREEN_HEIGHT - 20, footerBuf, 1, false, 0x00);
}

void EpubReader::renderCurrentPage() {
    if (!_isOpen || _pages.empty()) return;
    
    Display.clear(0xFF); // White background
    renderHeaderFooter();
    
    // Read page content from file
    size_t pageIdx = _progress.currentPage - 1;
    if (pageIdx < _pages.size()) {
        File f = SD.open(_filepath, FILE_READ);
        if (f) {
            f.seek(_pages[pageIdx].byteOffset);
            char buffer[1400];
            size_t bytesRead = f.readBytes(buffer, sizeof(buffer) - 1);
            buffer[bytesRead] = '\0';
            f.close();
            
            // Clean XML/HTML tags for plain text rendering if necessary
            int16_t x = 20;
            int16_t y = 45;
            Display.renderText(x, y, buffer, 1, false, 0x00);
        }
    }
    
    Display.update(RefreshMode::FAST_PARTIAL);
}
