#include "display_driver.h"

DisplayDriver Display;

DisplayDriver::DisplayDriver() : _frameBuffer(nullptr), _initialized(false) {}

bool DisplayDriver::init() {
    pinMode(PIN_EPD_CS, OUTPUT);
    digitalWrite(PIN_EPD_CS, HIGH); // Inactive initially
    
    pinMode(PIN_EPD_DC, OUTPUT);
    pinMode(PIN_EPD_RST, OUTPUT);
    pinMode(PIN_EPD_BUSY, INPUT);
    pinMode(PIN_EPD_PWR, OUTPUT);
    
    digitalWrite(PIN_EPD_PWR, HIGH); // Enable power to display
    
    // Allocate framebuffer in internal SRAM: 480 * 800 / 8 bytes = 48,000 bytes (~47 KB)
    const size_t fbSize = (SCREEN_WIDTH * SCREEN_HEIGHT) / 8;
    _frameBuffer = (uint8_t*)malloc(fbSize);
    
    if (!_frameBuffer) {
        Serial.println("[EPD] ERROR: Failed to allocate framebuffer");
        return false;
    }
    
    memset(_frameBuffer, 0xFF, fbSize); // Default white
    
    // Hardware reset with proper stabilization
    digitalWrite(PIN_EPD_RST, HIGH);
    delay(20);
    digitalWrite(PIN_EPD_RST, LOW);
    delay(30);
    digitalWrite(PIN_EPD_RST, HIGH);
    delay(50);
    waitBusy();
    
    Serial.println("[EPD] Initializing display controller (SSD1677 / UC8179 / UC8279)...");
    
    // 1. UC8179 / UC8279 Power On & Panel Setting Sequence
    sendCommand(0x04); // Power ON (UC8179)
    waitBusy();
    
    sendCommand(0x00); // Panel Setting (PSR)
    sendData(0x1F);    // 480x800 / KW-BF / LUT from OTP
    sendData(0x0D);
    
    // 2. SSD1677 Soft Reset & Driver Control Sequence
    sendCommand(0x12); // Software Reset (SSD1677)
    waitBusy();
    
    sendCommand(0x01); // Driver output control
    sendData((SCREEN_HEIGHT - 1) & 0xFF);
    sendData(((SCREEN_HEIGHT - 1) >> 8) & 0xFF);
    sendData(0x00);
    
    sendCommand(0x11); // Data entry mode: X increment, Y increment
    sendData(0x03);
    
    sendCommand(0x3C); // Border waveform
    sendData(0x05);
    
    _initialized = true;
    return true;
}

void DisplayDriver::powerOn() {
    digitalWrite(PIN_EPD_PWR, HIGH);
    sendCommand(0x22); // Display update control
    sendData(0xC0);
    sendCommand(0x20); // Master activation
    waitBusy();
}

void DisplayDriver::powerOff() {
    sendCommand(0x10); // Deep sleep mode
    sendData(0x01);
    digitalWrite(PIN_EPD_PWR, LOW);
}

void DisplayDriver::waitBusy() {
    unsigned long start = millis();
    while (digitalRead(PIN_EPD_BUSY) == HIGH) {
        delay(5);
        if (millis() - start > 4000) break; // Timeout safeguard
    }
}

void DisplayDriver::sendCommand(uint8_t cmd) {
    SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_EPD_DC, LOW);
    digitalWrite(PIN_EPD_CS, LOW);
    SPI.transfer(cmd);
    digitalWrite(PIN_EPD_CS, HIGH);
    SPI.endTransaction();
}

void DisplayDriver::sendData(uint8_t data) {
    SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_EPD_DC, HIGH);
    digitalWrite(PIN_EPD_CS, LOW);
    SPI.transfer(data);
    digitalWrite(PIN_EPD_CS, HIGH);
    SPI.endTransaction();
}

void DisplayDriver::sendDataBuffer(const uint8_t* data, size_t len) {
    SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_EPD_DC, HIGH);
    digitalWrite(PIN_EPD_CS, LOW);
    SPI.writeBytes(data, len);
    digitalWrite(PIN_EPD_CS, HIGH);
    SPI.endTransaction();
}

void DisplayDriver::clear(uint8_t color) {
    if (!_frameBuffer) return;
    size_t fbSize = (SCREEN_WIDTH * SCREEN_HEIGHT) / 8;
    memset(_frameBuffer, color == 0 ? 0x00 : 0xFF, fbSize);
}

void DisplayDriver::drawPixel(int16_t x, int16_t y, uint8_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT || !_frameBuffer) return;
    
    size_t byteIdx = (y * SCREEN_WIDTH + x) / 8;
    uint8_t bitMask = 0x80 >> (x & 7);
    
    if (color == 0) {
        _frameBuffer[byteIdx] &= ~bitMask; // Black (0)
    } else {
        _frameBuffer[byteIdx] |= bitMask;  // White (1)
    }
}

void DisplayDriver::drawFastHLine(int16_t x, int16_t y, int16_t w, uint8_t color) {
    for (int16_t i = 0; i < w; i++) {
        drawPixel(x + i, y, color);
    }
}

void DisplayDriver::drawFastVLine(int16_t x, int16_t y, int16_t h, uint8_t color) {
    for (int16_t i = 0; i < h; i++) {
        drawPixel(x, y + i, color);
    }
}

void DisplayDriver::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    for (int16_t j = 0; j < h; j++) {
        drawFastHLine(x, y + j, w, color);
    }
}

void DisplayDriver::drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint8_t color) {
    int16_t byteWidth = (w + 7) / 8;
    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            if (bitmap[j * byteWidth + i / 8] & (128 >> (i & 7))) {
                drawPixel(x + i, y + j, color);
            }
        }
    }
}

void DisplayDriver::drawDitheredImage(int16_t x, int16_t y, const uint8_t* gray4Buffer, int16_t w, int16_t h) {
    // Render 4-level grayscale (2 bits per pixel) using 2x2 ordered dither matrix
    static const uint8_t bayer2x2[2][2] = {
        { 0, 2 },
        { 3, 1 }
    };

    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            if (x + i >= SCREEN_WIDTH || y + j >= SCREEN_HEIGHT) continue;
            size_t pixelIndex = j * w + i;
            uint8_t byteVal = gray4Buffer[pixelIndex / 4];
            uint8_t shift = (3 - (pixelIndex % 4)) * 2;
            uint8_t grayLevel = (byteVal >> shift) & 0x03; // 0 (black), 1 (dark gray), 2 (light gray), 3 (white)

            uint8_t threshold = bayer2x2[(y + j) % 2][(x + i) % 2];
            uint8_t pixelColor = (grayLevel > threshold) ? 1 : 0;
            drawPixel(x + i, y + j, pixelColor);
        }
    }
}

// 5x7 ASCII font table
static const uint8_t font5x7[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, // (space)
    0x00, 0x00, 0x5F, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // $
    0x23, 0x13, 0x08, 0x64, 0x62, // %
    0x36, 0x49, 0x55, 0x22, 0x50, // &
    0x00, 0x05, 0x03, 0x00, 0x00, // '
    0x00, 0x1C, 0x22, 0x41, 0x00, // (
    0x00, 0x41, 0x22, 0x1C, 0x00, // )
    0x14, 0x08, 0x3E, 0x08, 0x14, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, // +
    0x00, 0x50, 0x30, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, // -
    0x00, 0x60, 0x60, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, // /
    0x3E, 0x51, 0x49, 0x45, 0x3E, // 0
    0x00, 0x42, 0x7F, 0x40, 0x00, // 1
    0x42, 0x61, 0x51, 0x49, 0x46, // 2
    0x21, 0x41, 0x45, 0x4B, 0x31, // 3
    0x18, 0x14, 0x12, 0x7F, 0x10, // 4
    0x27, 0x45, 0x45, 0x45, 0x39, // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
    0x01, 0x71, 0x09, 0x05, 0x03, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, // 8
    0x06, 0x49, 0x49, 0x29, 0x1E, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, // ;
    0x08, 0x14, 0x22, 0x41, 0x00, // <
    0x14, 0x14, 0x14, 0x14, 0x14, // =
    0x00, 0x41, 0x22, 0x14, 0x08, // >
    0x02, 0x01, 0x51, 0x09, 0x06, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, // @
    0x7E, 0x11, 0x11, 0x11, 0x7E, // A
    0x7F, 0x49, 0x49, 0x49, 0x36, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, // C
    0x7F, 0x41, 0x41, 0x22, 0x1C, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, // E
    0x7F, 0x09, 0x09, 0x09, 0x01, // F
    0x3E, 0x41, 0x49, 0x49, 0x7A, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, // H
    0x00, 0x41, 0x7F, 0x41, 0x00, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, // L
    0x7F, 0x02, 0x0C, 0x02, 0x7F, // M
    0x7F, 0x04, 0x08, 0x10, 0x7F, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
    0x7F, 0x09, 0x19, 0x29, 0x46, // R
    0x46, 0x49, 0x49, 0x49, 0x31, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, // V
    0x3F, 0x40, 0x38, 0x40, 0x3F, // W
    0x63, 0x14, 0x08, 0x14, 0x63, // X
    0x07, 0x08, 0x70, 0x08, 0x07, // Y
    0x61, 0x51, 0x49, 0x45, 0x43, // Z
    0x00, 0x7F, 0x41, 0x41, 0x00, // [
    0x02, 0x04, 0x08, 0x10, 0x20, // \
    0x00, 0x41, 0x41, 0x7F, 0x00, // ]
    0x04, 0x02, 0x01, 0x02, 0x04, // ^
    0x40, 0x40, 0x40, 0x40, 0x40, // _
    0x00, 0x01, 0x02, 0x04, 0x00, // `
    0x20, 0x54, 0x54, 0x54, 0x78, // a
    0x7F, 0x48, 0x44, 0x44, 0x38, // b
    0x38, 0x44, 0x44, 0x44, 0x20, // c
    0x38, 0x44, 0x44, 0x48, 0x7F, // d
    0x38, 0x54, 0x54, 0x54, 0x18, // e
    0x08, 0x7E, 0x09, 0x01, 0x02, // f
    0x0C, 0x52, 0x52, 0x52, 0x3E, // g
    0x7F, 0x08, 0x04, 0x04, 0x78, // h
    0x00, 0x44, 0x7D, 0x40, 0x00, // i
    0x20, 0x40, 0x44, 0x3D, 0x00, // j
    0x7F, 0x10, 0x28, 0x44, 0x00, // k
    0x00, 0x41, 0x7F, 0x40, 0x00, // l
    0x7C, 0x04, 0x18, 0x04, 0x78, // m
    0x7C, 0x08, 0x04, 0x04, 0x78, // n
    0x38, 0x44, 0x44, 0x44, 0x38, // o
    0x7C, 0x14, 0x14, 0x14, 0x08, // p
    0x08, 0x14, 0x14, 0x18, 0x7C, // q
    0x7C, 0x08, 0x04, 0x04, 0x08, // r
    0x48, 0x54, 0x54, 0x54, 0x20, // s
    0x04, 0x3F, 0x44, 0x40, 0x20, // t
    0x3C, 0x40, 0x40, 0x20, 0x7C, // u
    0x1C, 0x20, 0x40, 0x20, 0x1C, // v
    0x3C, 0x40, 0x30, 0x40, 0x3C, // w
    0x44, 0x28, 0x10, 0x28, 0x44, // x
    0x0C, 0x50, 0x50, 0x50, 0x3C, // y
    0x44, 0x64, 0x54, 0x4C, 0x44, // z
    0x00, 0x08, 0x36, 0x41, 0x00, // {
    0x00, 0x00, 0x7F, 0x00, 0x00, // |
    0x00, 0x41, 0x36, 0x08, 0x00, // }
    0x08, 0x08, 0x2A, 0x1C, 0x08  // ~
};

void DisplayDriver::renderText(int16_t x, int16_t y, const char* text, uint8_t size, bool bold, uint8_t color) {
    if (!text) return;
    int16_t cursorX = x;
    int16_t cursorY = y;
    
    while (*text) {
        char c = *text++;
        if (c == '\n') {
            cursorY += (8 * size) + 4;
            cursorX = x;
            continue;
        }
        if (c < 32 || c > 126) c = '?';
        
        uint16_t charIdx = (c - 32) * 5;
        for (uint8_t col = 0; col < 5; col++) {
            uint8_t line = pgm_read_byte(&font5x7[charIdx + col]);
            for (uint8_t row = 0; row < 7; row++) {
                if (line & (1 << row)) {
                    if (size == 1) {
                        drawPixel(cursorX + col, cursorY + row, color);
                        if (bold) drawPixel(cursorX + col + 1, cursorY + row, color);
                    } else {
                        fillRect(cursorX + col * size, cursorY + row * size, size + (bold ? 1 : 0), size, color);
                    }
                }
            }
        }
        cursorX += (6 * size) + (bold ? 1 : 0);
    }
}

void DisplayDriver::update(RefreshMode mode) {
    if (!_frameBuffer || !_initialized) return;
    
    const size_t fbSize = (SCREEN_WIDTH * SCREEN_HEIGHT) / 8;
    
    // 1. UC8179 / UC8279 Resolution & Buffer Transfer
    sendCommand(0x04); // Power ON (UC8179)
    waitBusy();
    
    sendCommand(0x61); // TRES (Resolution Setting)
    sendData((SCREEN_WIDTH >> 8) & 0xFF);
    sendData(SCREEN_WIDTH & 0xFF);
    sendData((SCREEN_HEIGHT >> 8) & 0xFF);
    sendData(SCREEN_HEIGHT & 0xFF);
    
    sendCommand(0x10); // DTM1 (Old buffer)
    sendDataBuffer(_frameBuffer, fbSize);
    
    sendCommand(0x13); // DTM2 (New buffer)
    sendDataBuffer(_frameBuffer, fbSize);
    
    // 2. SSD1677 RAM Window & Buffer Transfer
    sendCommand(0x44); // Set RAM X - start/end (0 to 65 for 528 width)
    sendData(0x00);
    sendData(((SCREEN_WIDTH + 7) / 8) - 1);
    
    sendCommand(0x45); // Set RAM Y - start/end (0 to 791 for 792 height)
    sendData(0x00);
    sendData(0x00);
    sendData((SCREEN_HEIGHT - 1) & 0xFF);
    sendData(((SCREEN_HEIGHT - 1) >> 8) & 0xFF);
    
    sendCommand(0x4E); // Set RAM X address counter
    sendData(0x00);
    
    sendCommand(0x4F); // Set RAM Y address counter
    sendData(0x00);
    sendData(0x00);
    
    sendCommand(0x24); // Write RAM (Black/White data)
    sendDataBuffer(_frameBuffer, fbSize);
    
    sendCommand(0x26); // Write RAM (Red data)
    sendDataBuffer(_frameBuffer, fbSize);
    
    // 3. Trigger Refresh Execution on both controller formats
    sendCommand(0x12); // UC8179 Display Refresh
    
    sendCommand(0x22); // SSD1677 Display Update Control
    sendData(mode == RefreshMode::FAST_PARTIAL ? 0xFF : 0xF7);
    
    sendCommand(0x20); // SSD1677 Master Activation
    waitBusy();
}
