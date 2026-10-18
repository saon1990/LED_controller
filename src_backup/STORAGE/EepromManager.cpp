#include "EepromManager.h"
#include "../config.h"

#include <Wire.h>

bool EepromManager::begin()
{
    uint8_t marker;

    if (readBytes(
            EEPROM_LED_VALID_ADDR,
            &marker,
            1))
    {
        Serial.println("[STORAGE] AT24C32 OK.");
        return true;
    }

    Serial.println("[STORAGE] BLAD: AT24C32 nie odpowiada.");
    return false;
}

bool EepromManager::writeBytes(
    uint16_t address,
    const uint8_t* data,
    uint8_t length)
{
    Wire.beginTransmission(EEPROM_ADDR);

    Wire.write((address >> 8) & 0xFF);
    Wire.write(address & 0xFF);

    for (uint8_t i = 0; i < length; i++)
        Wire.write(data[i]);

    if (Wire.endTransmission() != 0)
        return false;

    delay(10);

    return true;
}

bool EepromManager::readBytes(
    uint16_t address,
    uint8_t* data,
    uint8_t length)
{
    Wire.beginTransmission(EEPROM_ADDR);

    Wire.write((address >> 8) & 0xFF);
    Wire.write(address & 0xFF);

    if (Wire.endTransmission(false) != 0)
        return false;

    if (Wire.requestFrom(
            (uint8_t)EEPROM_ADDR,
            length) != length)
    {
        return false;
    }

    for (uint8_t i = 0; i < length; i++)
        data[i] = Wire.read();

    return true;
}

bool EepromManager::saveTimestamp(uint32_t timestamp)
{
    uint8_t buffer[4];

    buffer[0] = (timestamp >> 24) & 0xFF;
    buffer[1] = (timestamp >> 16) & 0xFF;
    buffer[2] = (timestamp >> 8) & 0xFF;
    buffer[3] = timestamp & 0xFF;

    return writeBytes(
        EEPROM_TIMESTAMP_ADDR,
        buffer,
        4
    );
}

bool EepromManager::loadTimestamp(uint32_t& timestamp)
{
    uint8_t buffer[4];

    if (!readBytes(
            EEPROM_TIMESTAMP_ADDR,
            buffer,
            4))
    {
        return false;
    }

    timestamp =
        ((uint32_t)buffer[0] << 24) |
        ((uint32_t)buffer[1] << 16) |
        ((uint32_t)buffer[2] << 8)  |
        ((uint32_t)buffer[3]);

    return true;
}

bool EepromManager::saveLedState(bool state)
{
    uint8_t value = state ? 0x01 : 0x00;

    if (!writeBytes(
            EEPROM_LED_STATE_ADDR,
            &value,
            1))
    {
        return false;
    }

    uint8_t marker = LED_VALID_MARKER;

    if (!writeBytes(
            EEPROM_LED_VALID_ADDR,
            &marker,
            1))
    {
        return false;
    }

    return true;
}

bool EepromManager::loadLedState(bool& state)
{
    uint8_t valid;
    uint8_t value;

    if (!readBytes(
            EEPROM_LED_VALID_ADDR,
            &valid,
            1))
    {
        return false;
    }

    if (valid != LED_VALID_MARKER)
        return false;

    if (!readBytes(
            EEPROM_LED_STATE_ADDR,
            &value,
            1))
    {
        return false;
    }

    if (value == 0x00)
    {
        state = false;
        return true;
    }

    if (value == 0x01)
    {
        state = true;
        return true;
    }

    return false;
}
