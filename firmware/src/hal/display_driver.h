#pragma once

#include <Arduino.h>
#include <SPI.h>
#include "device_pins.h"

enum class RefreshMode {
    FAST_PARTIAL,   // Quick 1-bit partial refresh for fast page turns (<150ms)
    GRAYSCALE_4LVL, // 4-level grayscale refresh for images/covers
    FULL_CLEAR      // Full E-ink refresh to clear ghosting
};

class DisplayDriver {
public:
    DisplayDriver();
    bool init();
    void powerOn();
    void powerOff();
    
    void clear(uint8_t color = 0xFF);
    void drawPixel(int16_t x, int16_t y, uint8_t color);
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint8_t color);
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint8_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
    void drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint8_t color);
    void drawDitheredImage(int16_t x, int16_t y, const uint8_t* gray4Buffer, int16_t w, int16_t h);
    
    void renderText(int16_t x, int16_t y, const char* text, uint8_t size = 1, bool bold = false, uint8_t color = 0x00);
    
    void update(RefreshMode mode = RefreshMode::FAST_PARTIAL);
    
    int16_t width() const { return SCREEN_WIDTH; }
    int16_t height() const { return SCREEN_HEIGHT; }
    
    uint8_t* getFrameBuffer() { return _frameBuffer; }

private:
    void sendCommand(uint8_t cmd);
    void sendData(uint8_t data);
    void sendDataBuffer(const uint8_t* data, size_t len);
    void waitBusy();

    uint8_t* _frameBuffer;
    bool _initialized;
};

extern DisplayDriver Display;
