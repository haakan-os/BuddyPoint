#pragma once

#include <Arduino.h>
#include <vector>
#include "../hal/display_driver.h"
#include "../reader/book_cache.h"
#include "../reader/epub_reader.h"
#include "config.h"

enum class ViewState {
    HOME_SHELF,
    READER,
    SYNC_MENU,
    SLEEP_LOCKSCREEN
};

class UIManager {
public:
    UIManager();
    
    void init();
    void setViewState(ViewState state);
    ViewState getViewState() const { return _currentView; }
    
    void render();
    void handleButtonPress(uint8_t pin);
    
    void showSyncNotification(const char* message);
    void renderSleepScreen();

private:
    ViewState _currentView;
    std::vector<BookItem> _books;
    size_t _selectedBookIndex;
    
    void renderHomeShelf();
    void renderSyncMenu();
    void renderStatusBar();
};

extern UIManager UI;
