#pragma once

#include <Arduino.h>

class EepromManager
{
public:
    bool begin();

    bool saveTimestamp(uint32_t timestamp);
    bool loadTimestamp(uint32_t& timestamp);

    bool saveLedMode(uint8_t mode);
    bool loadLedMode(uint8_t& mode);

    // Ostatni aktywny tryb, niezależny od aktualnego OFF.
    bool saveLastActiveLedMode(uint8_t mode);
    bool loadLastActiveLedMode(uint8_t& mode);

private:
    bool writeBytes(
        uint16_t address,
        const uint8_t* data,
        uint8_t length
    );

    bool readBytes(
        uint16_t address,
        uint8_t* data,
        uint8_t length
    );
};