#include "ui_manager.h"
#include "../hal/power_mgr.h"
#include "../sync/kosync_client.h"
#include "../sync/book_sync_server.h"

UIManager UI;

UIManager::UIManager() : _currentView(ViewState::HOME_SHELF), _selectedBookIndex(0) {}

void UIManager::init() {
    Books.scanBooks(_books);
    _currentView = ViewState::HOME_SHELF;
}

void UIManager::setViewState(ViewState state) {
    _currentView = state;
    render();
}

void UIManager::renderStatusBar() {
    // Battery and Wi-Fi header
    char statusBuf[64];
    uint8_t bat = Power.getBatteryPercentage();
    bool wifi = KOSync.isWifiConnected();
    
    snprintf(statusBuf, sizeof(statusBuf), "%s | Bat: %u%%%s", wifi ? "Wi-Fi: ON" : "Wi-Fi: OFF", bat, Power.isCharging() ? " +" : "");
    Display.renderText(16, 8, statusBuf, 1, false, 0x00);
    Display.drawFastHLine(16, 24, SCREEN_WIDTH - 32, 0x00);
}

void UIManager::renderHomeShelf() {
    Display.clear(0xFF);
    renderStatusBar();
    
    Display.renderText(20, 40, "HAAKANPOINT", 2, true, 0x00);
    Display.renderText(20, 65, "On-The-Go Buddy for KOReader", 1, false, 0x00);
    Display.drawFastHLine(20, 80, SCREEN_WIDTH - 40, 0x00);
    
    // Quick-Resume / Currently Reading Card
    Display.fillRect(20, 95, SCREEN_WIDTH - 40, 110, 0xFF);
    Display.drawFastHLine(20, 95, SCREEN_WIDTH - 40, 0x00);
    Display.drawFastHLine(20, 205, SCREEN_WIDTH - 40, 0x00);
    Display.drawFastVLine(20, 95, 110, 0x00);
    Display.drawFastVLine(SCREEN_WIDTH - 20, 95, 110, 0x00);
    
    Display.renderText(35, 110, "CURRENTLY READING", 1, true, 0x00);
    
    ReadingProgress lastRead;
    if (Books.loadCurrentState(lastRead) && strlen(lastRead.bookTitle) > 0) {
        Display.renderText(35, 130, lastRead.bookTitle, 1, false, 0x00);
        
        char progStr[64];
        snprintf(progStr, sizeof(progStr), "Progress: %.1f%%  (Page %u of %u)", lastRead.percentage, lastRead.currentPage, lastRead.totalPages);
        Display.renderText(35, 150, progStr, 1, false, 0x00);
        
        // Progress Bar
        Display.drawFastHLine(35, 175, 200, 0x00);
        int16_t filledW = (int16_t)(200.0f * (lastRead.percentage / 100.0f));
        if (filledW > 0) Display.fillRect(35, 173, filledW, 5, 0x00);
    } else {
        Display.renderText(35, 140, "No active book. Send from KOReader!", 1, false, 0x00);
    }
    
    // Book List
    Display.renderText(20, 225, "LIBRARY BOOKS", 1, true, 0x00);
    Display.drawFastHLine(20, 240, SCREEN_WIDTH - 40, 0x00);
    
    if (_books.empty()) {
        Display.renderText(35, 260, "No books on MicroSD card.", 1, false, 0x00);
        Display.renderText(35, 280, "Pair Wi-Fi to push books wirelessly.", 1, false, 0x00);
    } else {
        int16_t y = 260;
        for (size_t i = 0; i < _books.size() && i < 6; i++) {
            if (i == _selectedBookIndex) {
                Display.fillRect(20, y - 2, SCREEN_WIDTH - 40, 24, 0x00);
                Display.renderText(25, y + 4, ("> " + _books[i].title).c_str(), 1, true, 0xFF);
            } else {
                Display.renderText(25, y + 4, ("  " + _books[i].title).c_str(), 1, false, 0x00);
            }
            y += 28;
        }
    }
    
    // Footer Navigation Controls
    Display.drawFastHLine(20, SCREEN_HEIGHT - 40, SCREEN_WIDTH - 40, 0x00);
    Display.renderText(25, SCREEN_HEIGHT - 28, "[PREV/NEXT] Select  |  [HOME] Open / Sync", 1, false, 0x00);
    
    Display.update(RefreshMode::FAST_PARTIAL);
}

void UIManager::renderSyncMenu() {
    Display.clear(0xFF);
    renderStatusBar();
    
    Display.renderText(20, 40, "KOREADER PAIRING", 2, true, 0x00);
    Display.drawFastHLine(20, 65, SCREEN_WIDTH - 40, 0x00);
    
    int16_t y = 80;
    Display.renderText(25, y, "1. Wi-Fi Status:", 1, true, 0x00);
    Display.renderText(35, y + 18, KOSync.isWifiConnected() ? "Connected" : "Disconnected", 1, false, 0x00);
    
    y += 45;
    Display.renderText(25, y, "2. Direct Pairing Address:", 1, true, 0x00);
    Display.renderText(35, y + 18, "http://haakanpoint.local/api/upload", 1, false, 0x00);
    if (WiFi.status() == WL_CONNECTED) {
        Display.renderText(35, y + 36, ("IP: " + WiFi.localIP().toString()).c_str(), 1, false, 0x00);
    }
    
    y += 65;
    Display.renderText(25, y, "3. KOSync Cloud Server:", 1, true, 0x00);
    Display.renderText(35, y + 18, "sync.koreader.rocks", 1, false, 0x00);
    
    y += 50;
    Display.renderText(25, y, "Press [HOME] to trigger sync & resume reading", 1, false, 0x00);
    
    Display.update(RefreshMode::FAST_PARTIAL);
}

void UIManager::renderSleepScreen() {
    Display.clear(0xFF);
    
    Display.drawFastHLine(30, 80, SCREEN_WIDTH - 60, 0x00);
    Display.renderText(35, 100, "HAAKANPOINT", 2, true, 0x00);
    Display.renderText(35, 130, "On-The-Go Companion", 1, false, 0x00);
    Display.drawFastHLine(30, 150, SCREEN_WIDTH - 60, 0x00);
    
    ReadingProgress current;
    if (Books.loadCurrentState(current) && strlen(current.bookTitle) > 0) {
        Display.renderText(40, 240, "CURRENT BOOK", 1, true, 0x00);
        Display.renderText(40, 270, current.bookTitle, 2, true, 0x00);
        
        char buf[64];
        snprintf(buf, sizeof(buf), "Reading Progress: %.1f%%", current.percentage);
        Display.renderText(40, 320, buf, 1, false, 0x00);
        
        Display.drawFastHLine(40, 350, 300, 0x00);
        int16_t w = (int16_t)(300.0f * (current.percentage / 100.0f));
        if (w > 0) Display.fillRect(40, 348, w, 5, 0x00);
    }
    
    char batBuf[32];
    snprintf(batBuf, sizeof(batBuf), "Battery: %u%%", Power.getBatteryPercentage());
    Display.renderText(40, SCREEN_HEIGHT - 80, batBuf, 1, false, 0x00);
    Display.renderText(40, SCREEN_HEIGHT - 55, "Press any button to wake", 1, false, 0x00);
    
    Display.update(RefreshMode::FULL_CLEAR);
}

void UIManager::showSyncNotification(const char* message) {
    // Popup notification banner
    Display.fillRect(30, SCREEN_HEIGHT / 2 - 30, SCREEN_WIDTH - 60, 60, 0xFF);
    Display.drawFastHLine(30, SCREEN_HEIGHT / 2 - 30, SCREEN_WIDTH - 60, 0x00);
    Display.drawFastHLine(30, SCREEN_HEIGHT / 2 + 30, SCREEN_WIDTH - 60, 0x00);
    Display.drawFastVLine(30, SCREEN_HEIGHT / 2 - 30, 60, 0x00);
    Display.drawFastVLine(SCREEN_WIDTH - 30, SCREEN_HEIGHT / 2 - 30, 60, 0x00);
    
    Display.renderText(45, SCREEN_HEIGHT / 2 - 8, message, 1, true, 0x00);
    Display.update(RefreshMode::FAST_PARTIAL);
}

void UIManager::render() {
    switch (_currentView) {
        case ViewState::HOME_SHELF:
            renderHomeShelf();
            break;
        case ViewState::READER:
            Reader.renderCurrentPage();
            break;
        case ViewState::SYNC_MENU:
            renderSyncMenu();
            break;
        case ViewState::SLEEP_LOCKSCREEN:
            renderSleepScreen();
            break;
    }
}

void UIManager::handleButtonPress(uint8_t pin) {
    Power.resetSleepTimer();
    
    if (_currentView == ViewState::HOME_SHELF) {
        if (pin == PIN_BTN_NEXT) {
            if (!_books.empty() && _selectedBookIndex < _books.size() - 1) {
                _selectedBookIndex++;
                renderHomeShelf();
            }
        } else if (pin == PIN_BTN_PREV) {
            if (_selectedBookIndex > 0) {
                _selectedBookIndex--;
                renderHomeShelf();
            }
        } else if (pin == PIN_BTN_HOME) {
            if (!_books.empty() && _selectedBookIndex < _books.size()) {
                if (Reader.openBook(_books[_selectedBookIndex].fullPath)) {
                    // Check KOSync for newer progress
                    ReadingProgress remote;
                    if (KOSync.pullProgress(Reader.getProgress().documentHash, remote) == SyncResult::SUCCESS) {
                        if (remote.percentage > Reader.getPercentage()) {
                            Reader.jumpToPercentage(remote.percentage);
                        }
                    }
                    _currentView = ViewState::READER;
                    render();
                }
            }
        }
    } else if (_currentView == ViewState::READER) {
        if (pin == PIN_BTN_NEXT) {
            if (Reader.nextPage()) {
                Reader.renderCurrentPage();
            }
        } else if (pin == PIN_BTN_PREV) {
            if (Reader.prevPage()) {
                Reader.renderCurrentPage();
            }
        } else if (pin == PIN_BTN_HOME) {
            // Save & Sync before exiting
            showSyncNotification("Syncing with KOReader...");
            KOSync.pushProgress(Reader.getProgress());
            Reader.closeBook();
            Books.scanBooks(_books);
            _currentView = ViewState::HOME_SHELF;
            render();
        }
    }
}
