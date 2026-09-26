#pragma once

// Hardware pin mappings for Xteink X3 (ESP32-C3)
// 3.7" E-ink Display (SSD1677 / UC8179) & Peripherals

#define PIN_EPD_SCK     8   // SPI Clock (GPIO 8)
#define PIN_EPD_MOSI    10  // SPI MOSI (GPIO 10)
#define PIN_EPD_CS      21  // Display Chip Select (GPIO 21)
#define PIN_EPD_DC      4   // Display Data/Command (GPIO 4)
#define PIN_EPD_RST     5   // Display Reset (GPIO 5)
#define PIN_EPD_BUSY    6   // Display Busy (GPIO 6)
#define PIN_EPD_PWR     3   // Display Power EN (GPIO 3)

// MicroSD Card (Shared SPI bus)
#define PIN_SD_CS       12  // SD Chip Select (GPIO 12)
#define PIN_SD_SCK      8   // Shared SPI Clock (GPIO 8)
#define PIN_SD_MOSI     10  // Shared SPI MOSI (GPIO 10)
#define PIN_SD_MISO     7   // SPI MISO (GPIO 7)
#define PIN_SD_PWR      13  // SD Power Rail (GPIO 13)

// User Buttons
#define PIN_BTN_PREV    0   // Previous page (GPIO 0)
#define PIN_BTN_NEXT    1   // Next page (GPIO 1)
#define PIN_BTN_HOME    3   // Home / Power Wake (GPIO 3)

// Battery Voltage ADC Monitor
#define PIN_BATTERY_ADC 1
#define BATTERY_VOLTAGE_DIVIDER_RATIO 2.0f

// Native Xteink X3 Screen Dimensions (528 x 792)
#define SCREEN_WIDTH    528
#define SCREEN_HEIGHT   792
