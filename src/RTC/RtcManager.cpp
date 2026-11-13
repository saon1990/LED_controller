
#include "RtcManager.h"
#include "../config.h"

#include <Wire.h>
#include <stdio.h>
#include <string.h>

// ============================================================
// RTC INIT
// ============================================================

bool RtcManager::begin()
{
    RtcDateTime now;
    bool oscillatorStopped = false;

    if (!getDateTime(now))
    {
        Serial.println("[RTC] BLAD ODCZYTU DS3231.");
        return false;
    }

    if (!isOscillatorStopped(oscillatorStopped))
    {
        Serial.println("[RTC] BLAD ODCZYTU STATUSU DS3231.");
        return false;
    }

    bool dateValid = isDateTimeValid(now);

    // --------------------------------------------------------
    // CHECK RTC STATE
    // --------------------------------------------------------

    if (oscillatorStopped || !dateValid)
    {
        Serial.println();

        if (oscillatorStopped)
        {
            Serial.println(
                "[RTC] Wykryto utrate zasilania oscylatora (OSF)."
            );
        }

        if (!dateValid)
        {
            Serial.println(
                "[RTC] Niepoprawna data lub godzina."
            );
        }

        Serial.println(
            "[RTC] Ustawiam czas na podstawie kompilacji firmware."
        );

        if (!setTimeFromBuild())
        {
            Serial.println(
                "[RTC] BLAD USTAWIANIA CZASU."
            );

            return false;
        }

        if (!getDateTime(now))
        {
            Serial.println(
                "[RTC] BLAD ODCZYTU PO USTAWIENIU CZASU."
            );

            return false;
        }

        Serial.println(
            "[RTC] Czas zostal ustawiony."
        );
    }

    // --------------------------------------------------------
    // PRINT CURRENT TIME
    // --------------------------------------------------------

    Serial.printf(
        "[RTC] OK: %04u-%02u-%02u %02u:%02u:%02u\n",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second
    );

    return true;
}

// ============================================================
// BCD CONVERSION
// ============================================================

uint8_t RtcManager::bcdToDec(uint8_t value) const
{
    return ((value >> 4) * 10) + (value & 0x0F);
}

uint8_t RtcManager::decToBcd(uint8_t value) const
{
    return ((value / 10) << 4) | (value % 10);
}

// ============================================================
// MONTH CONVERSION
// ============================================================

uint8_t RtcManager::monthFromString(const char* month) const
{
    const char* months[] =
    {
        "Jan", "Feb", "Mar", "Apr",
        "May", "Jun", "Jul", "Aug",
        "Sep", "Oct", "Nov", "Dec"
    };

    for (uint8_t i = 0; i < 12; i++)
    {
        if (strncmp(month, months[i], 3) == 0)
            return i + 1;
    }

    return 0;
}

// ============================================================
// READ DATE AND TIME
// ============================================================

bool RtcManager::getDateTime(RtcDateTime& dateTime)
{
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(0x00);

    if (Wire.endTransmission(false) != 0)
        return false;

    if (Wire.requestFrom(
            (uint8_t)RTC_ADDR,
            (uint8_t)7) != 7)
    {
        return false;
    }

    uint8_t sec  = Wire.read();
    uint8_t min  = Wire.read();
    uint8_t hrs  = Wire.read();

    Wire.read(); // Dzien tygodnia

    uint8_t date = Wire.read();
    uint8_t mon  = Wire.read();
    uint8_t yr   = Wire.read();

    dateTime.second = bcdToDec(sec & 0x7F);
    dateTime.minute = bcdToDec(min & 0x7F);

    // --------------------------------------------------------
    // HOUR: 12H OR 24H MODE
    // --------------------------------------------------------

    if (hrs & 0x40)
    {
        // 12-hour mode

        uint8_t hour12 = bcdToDec(hrs & 0x1F);
        bool pm = hrs & 0x20;

        dateTime.hour = hour12 % 12;

        if (pm)
            dateTime.hour += 12;
    }
    else
    {
        // 24-hour mode

        dateTime.hour = bcdToDec(hrs & 0x3F);
    }

    dateTime.day   = bcdToDec(date & 0x3F);
    dateTime.month = bcdToDec(mon & 0x1F);
    dateTime.year  = 2000 + bcdToDec(yr);

    return true;
}

// ============================================================
// VALIDATE DATE AND TIME
// ============================================================

bool RtcManager::isDateTimeValid(
    const RtcDateTime& dateTime) const
{
    // DS3231 supports years 2000-2099.
    // We treat dates before 2024 as uninitialized.

    if (dateTime.year < 2024 || dateTime.year > 2099)
        return false;

    if (dateTime.month < 1 || dateTime.month > 12)
        return false;

    if (
        dateTime.day < 1 ||
        dateTime.day > daysInMonth(dateTime.year, dateTime.month)
    )
    {
        return false;
    }

    if (dateTime.hour > 23)
        return false;

    if (dateTime.minute > 59)
        return false;

    if (dateTime.second > 59)
        return false;

    return true;
}

// ============================================================
// READ OSCILLATOR STOP FLAG
// ============================================================

bool RtcManager::isOscillatorStopped(bool& stopped)
{
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(0x0F); // Status register

    if (Wire.endTransmission(false) != 0)
        return false;

    if (Wire.requestFrom(
            (uint8_t)RTC_ADDR,
            (uint8_t)1) != 1)
    {
        return false;
    }

    uint8_t status = Wire.read();

    // Bit 7 = OSF (Oscillator Stop Flag)

    stopped = status & 0x80;

    return true;
}

// ============================================================
// SET DATE AND TIME
// ============================================================

bool RtcManager::setDateTime(const RtcDateTime& dateTime)
{
    if (!isDateTimeValid(dateTime))
        return false;

    // --------------------------------------------------------
    // WRITE TIME REGISTERS
    // --------------------------------------------------------

    Wire.beginTransmission(RTC_ADDR);

    Wire.write(0x00);

    Wire.write(decToBcd(dateTime.second));
    Wire.write(decToBcd(dateTime.minute));
    Wire.write(decToBcd(dateTime.hour));

    Wire.write(0x01); // Day of week

    Wire.write(decToBcd(dateTime.day));
    Wire.write(decToBcd(dateTime.month));
    Wire.write(decToBcd(dateTime.year - 2000));

    if (Wire.endTransmission() != 0)
        return false;

    // --------------------------------------------------------
    // CLEAR OSCILLATOR STOP FLAG
    // --------------------------------------------------------

    Wire.beginTransmission(RTC_ADDR);
    Wire.write(0x0F);

    // OSF is bit 7. Preserve the remaining status bits.

    Wire.write(0x00);

    if (Wire.endTransmission() != 0)
        return false;

    return true;
}

// ============================================================
// SET TIME FROM FIRMWARE BUILD DATE
// ============================================================

bool RtcManager::setTimeFromBuild()
{
    RtcDateTime buildTime = {};

    char monthText[4] = {};
    int day = 0;
    int year = 0;

    int hour = 0;
    int minute = 0;
    int second = 0;

    // --------------------------------------------------------
    // PARSE __DATE__
    // Example: "Nov 13 2026"
    // --------------------------------------------------------

    if (sscanf(
            __DATE__,
            "%3s %d %d",
            monthText,
            &day,
            &year) != 3)
    {
        return false;
    }

    // --------------------------------------------------------
    // PARSE __TIME__
    // Example: "08:45:00"
    // --------------------------------------------------------

    if (sscanf(
            __TIME__,
            "%d:%d:%d",
            &hour,
            &minute,
            &second) != 3)
    {
        return false;
    }

    buildTime.year   = year;
    buildTime.month  = monthFromString(monthText);
    buildTime.day    = day;
    buildTime.hour   = hour;
    buildTime.minute = minute;
    buildTime.second = second;

    if (!isDateTimeValid(buildTime))
        return false;

    Serial.printf(
        "[RTC] Build time: %04u-%02u-%02u %02u:%02u:%02u\n",
        buildTime.year,
        buildTime.month,
        buildTime.day,
        buildTime.hour,
        buildTime.minute,
        buildTime.second
    );

    return setDateTime(buildTime);
}

// ============================================================
// DAYS IN MONTH
// ============================================================

uint8_t RtcManager::daysInMonth(
    uint16_t year,
    uint8_t month) const
{
    const uint8_t days[] =
    {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12)
        return 0;

    if (month == 2)
    {
        bool leap =
            ((year % 4 == 0) &&
             ((year % 100 != 0) ||
              (year % 400 == 0)));

        return leap ? 29 : 28;
    }

    return days[month - 1];
}

// ============================================================
// CONVERT DATE TO TIMESTAMP
// ============================================================

uint32_t RtcManager::toTimestamp(
    const RtcDateTime& dateTime) const
{
    uint32_t days = 0;

    for (uint16_t y = 2000; y < dateTime.year; y++)
    {
        bool leap =
            ((y % 4 == 0) &&
             ((y % 100 != 0) ||
              (y % 400 == 0)));

        days += leap ? 366 : 365;
    }

    for (uint8_t m = 1; m < dateTime.month; m++)
        days += daysInMonth(dateTime.year, m);

    days += dateTime.day - 1;

    return
        days * 86400UL +
        (uint32_t)dateTime.hour * 3600UL +
        (uint32_t)dateTime.minute * 60UL +
        dateTime.second;
}
