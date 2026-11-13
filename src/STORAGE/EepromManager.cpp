#include "EepromManager.h"
#include "../config.h"

#include <Wire.h>

bool EepromManager::begin()
{
    uint8_t marker;

    if (readBytes(EEPROM_LED_VALID_ADDR, &marker, 1))
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
            static_cast<uint8_t>(EEPROM_ADDR),
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

    if (!readBytes(EEPROM_TIMESTAMP_ADDR, buffer, 4))
        return false;

    timestamp =
        ((uint32_t)buffer[0] << 24) |
        ((uint32_t)buffer[1] << 16) |
        ((uint32_t)buffer[2] << 8) |
        ((uint32_t)buffer[3]);

    return true;
}

bool EepromManager::saveLedMode(uint8_t mode)
{
    if (mode > 0x02)
    {
        Serial.println("[STORAGE] BLAD: nieprawidlowy tryb LED.");
        return false;
    }

    uint8_t invalidMarker = 0x00;

    if (!writeBytes(EEPROM_LED_VALID_ADDR, &invalidMarker, 1))
        return false;

    if (!writeBytes(EEPROM_LED_STATE_ADDR, &mode, 1))
        return false;

    uint8_t validMarker = LED_VALID_MARKER;

    if (!writeBytes(EEPROM_LED_VALID_ADDR, &validMarker, 1))
        return false;

    Serial.printf(
        "[STORAGE] Zapisano tryb LED: 0x%02X\n",
        mode
    );

    return true;
}

bool EepromManager::loadLedMode(uint8_t& mode)
{
    uint8_t validMarker;
    uint8_t storedMode;

    if (!readBytes(EEPROM_LED_VALID_ADDR, &validMarker, 1))
        return false;

    if (validMarker != LED_VALID_MARKER)
    {
        Serial.println("[STORAGE] Brak poprawnego zapisu trybu LED.");
        return false;
    }

    if (!readBytes(EEPROM_LED_STATE_ADDR, &storedMode, 1))
        return false;

    if (storedMode > 0x02)
    {
        Serial.println("[STORAGE] Nieprawidlowy tryb LED w EEPROM.");
        return false;
    }

    mode = storedMode;

    Serial.printf(
        "[STORAGE] Odczytano tryb LED: 0x%02X\n",
        mode
    );

    return true;
}

// ============================================================
// LAST ACTIVE LED MODE
// Address 0x0016
// 0x01 = STATIC WHITE
// 0x02 = FLOW
// 0x00 and other values = invalid
// ============================================================

bool EepromManager::saveLastActiveLedMode(uint8_t mode)
{
    if (mode != 0x01 && mode != 0x02)
        return false;

    if (!writeBytes(
            EEPROM_LAST_ACTIVE_MODE_ADDR,
            &mode,
            1))
    {
        Serial.println(
            "[STORAGE] BLAD ZAPISU OSTATNIEGO TRYBU."
        );

        return false;
    }

    Serial.printf(
        "[STORAGE] Ostatni aktywny tryb: 0x%02X\n",
        mode
    );

    return true;
}

bool EepromManager::loadLastActiveLedMode(uint8_t& mode)
{
    uint8_t storedMode = 0xFF;

    if (!readBytes(
            EEPROM_LAST_ACTIVE_MODE_ADDR,
            &storedMode,
            1))
    {
        return false;
    }

    if (storedMode != 0x01 && storedMode != 0x02)
        return false;

    mode = storedMode;

    Serial.printf(
        "[STORAGE] Odczytano ostatni aktywny tryb: 0x%02X\n",
        mode
    );

    return true;
}