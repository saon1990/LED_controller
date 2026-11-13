#pragma once

#include <Arduino.h>

class EepromManager
{
public:
    bool begin();

    // --------------------------------------------------------
    // TIMESTAMP
    // --------------------------------------------------------

    bool saveTimestamp(uint32_t timestamp);
    bool loadTimestamp(uint32_t& timestamp);

    // --------------------------------------------------------
    // LED MODE
    // --------------------------------------------------------

    bool saveLedMode(uint8_t mode);
    bool loadLedMode(uint8_t& mode);

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