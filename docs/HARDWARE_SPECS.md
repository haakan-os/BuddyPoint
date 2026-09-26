# Xteink X3 Hardware Specifications & Pinout

## Overview
The **Xteink X3** is an ultra-compact "credit-card" sized e-paper reader powered by an Espressif ESP32-C3 RISC-V microcontroller.

## Hardware Summary

| Component | Specification |
| :--- | :--- |
| **Microcontroller** | Espressif ESP32-C3 (Single-core 32-bit RISC-V @ 160MHz) |
| **Internal RAM** | 400 KB SRAM (380 KB usable) |
| **Flash Storage** | 4 MB SPI Flash (Quad SPI) |
| **Display** | 3.7" E-Ink Screen (480 x 800 resolution, ~254 PPI) |
| **Display Controller** | SSD1677 / UC8176 compatible (4-level grayscale, partial refresh) |
| **Storage** | MicroSD Card slot (SPI interface, up to 32GB FAT32) |
| **Wireless** | 2.4 GHz Wi-Fi 4 (802.11 b/g/n) & Bluetooth 5 (LE) |
| **Buttons** | 3 tactile buttons (Previous/Up, Next/Down, Home/Power) |
| **Battery & Charging** | Li-Po battery with TP4056 charge IC, USB Type-C |
| **Power Management** | Deep sleep support with wake-on-button (<25µA sleep current) |

---

## GPIO Pinout Mapping

```
                ┌───────────────────────────────────┐
                │          ESP32-C3 PINOUT          │
                ├───────────────┬───────────────────┤
                │ GPIO 0        │ Button Previous   │
                │ GPIO 1        │ Button Next / ADC │
                │ GPIO 2        │ MicroSD CS        │
                │ GPIO 3        │ Display Power EN  │
                │ GPIO 4        │ SPI SCK           │
                │ GPIO 5        │ SPI MISO (SD)     │
                │ GPIO 6        │ SPI MOSI          │
                │ GPIO 7        │ Display CS        │
                │ GPIO 8        │ Display DC        │
                │ GPIO 9        │ Display RST       │
                │ GPIO 10       │ Display BUSY      │
                │ GPIO 20       │ Button Home/Wake  │
                └───────────────┴───────────────────┘
```

---

## Power Consumption & Battery Life
* **Active Reading (Screen Static):** ~12 mA (MCU in low frequency mode).
* **Page Turn (E-ink Refresh):** ~45 mA for ~120ms.
* **Wi-Fi Sync (Active Burst):** ~85–120 mA for 2–4 seconds during book upload / KOSync.
* **Deep Sleep:** ~25 µA (lasts weeks on standby).
