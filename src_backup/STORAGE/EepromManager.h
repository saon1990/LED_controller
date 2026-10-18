#pragma once

#include <Arduino.h>

class EepromManager
{
public:
    bool begin();

    bool saveTimestamp(uint32_t timestamp);
    bool loadTimestamp(uint32_t& timestamp);

    bool saveLedState(bool state);
    bool loadLedState(bool& state);

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
