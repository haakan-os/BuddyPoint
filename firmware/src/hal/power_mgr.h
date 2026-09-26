#pragma once

#include <Arduino.h>
#include <esp_sleep.h>
#include "device_pins.h"

class PowerManager {
public:
    PowerManager();
    void init();
    
    float getBatteryVoltage();
    uint8_t getBatteryPercentage();
    bool isCharging();
    
    void enterDeepSleep(uint32_t sleepSeconds = 0);
    void enterLightSleep(uint32_t sleepMs);
    
    void resetSleepTimer();
    bool shouldAutoSleep(uint32_t timeoutSeconds);

private:
    uint32_t _lastActivityTime;
};

extern PowerManager Power;
