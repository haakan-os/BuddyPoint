#include "power_mgr.h"

PowerManager Power;

PowerManager::PowerManager() : _lastActivityTime(0) {}

void PowerManager::init() {
    pinMode(PIN_BATTERY_ADC, INPUT);
    analogReadResolution(12); // 12-bit ADC (0-4095)
    _lastActivityTime = millis();
}

float PowerManager::getBatteryVoltage() {
    // Read raw ADC value (3.3V reference)
    uint32_t raw = analogRead(PIN_BATTERY_ADC);
    float pinVoltage = (raw / 4095.0f) * 3.3f;
    // Account for voltage divider
    return pinVoltage * BATTERY_VOLTAGE_DIVIDER_RATIO;
}

uint8_t PowerManager::getBatteryPercentage() {
    float voltage = getBatteryVoltage();
    if (voltage >= 4.20f) return 100;
    if (voltage <= 3.30f) return 0;
    
    // Linear approximation between 3.3V (0%) and 4.2V (100%)
    float pct = ((voltage - 3.30f) / (4.20f - 3.30f)) * 100.0f;
    if (pct > 100.0f) pct = 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    return (uint8_t)pct;
}

bool PowerManager::isCharging() {
    // If voltage is higher than full threshold or rising rapidly
    return getBatteryVoltage() > 4.25f;
}

void PowerManager::resetSleepTimer() {
    _lastActivityTime = millis();
}

bool PowerManager::shouldAutoSleep(uint32_t timeoutSeconds) {
    if (timeoutSeconds == 0) return false;
    return (millis() - _lastActivityTime) > (timeoutSeconds * 1000UL);
}

void PowerManager::enterDeepSleep(uint32_t sleepSeconds) {
    Serial.println("[POWER] Entering deep sleep...");
    
    // Configure wake-up source: Home / Power button (Active LOW)
    esp_deep_sleep_enable_gpio_wakeup((1ULL << PIN_BTN_HOME) | (1ULL << PIN_BTN_NEXT) | (1ULL << PIN_BTN_PREV), ESP_GPIO_WAKEUP_GPIO_LOW);
    
    if (sleepSeconds > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);
    }
    
    esp_deep_sleep_start();
}

void PowerManager::enterLightSleep(uint32_t sleepMs) {
    esp_sleep_enable_timer_wakeup((uint64_t)sleepMs * 1000ULL);
    esp_light_sleep_start();
}
