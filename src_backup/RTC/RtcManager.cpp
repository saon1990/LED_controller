#include "RtcManager.h"
#include "../config.h"

#include <Wire.h>

bool RtcManager::begin()
{
    RtcDateTime now;

    if (!getDateTime(now))
    {
        Serial.println("[RTC] BLAD: DS3231 nie odpowiada.");
        return false;
    }

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

uint8_t RtcManager::bcdToDec(uint8_t value) const
{
    return ((value >> 4) * 10) + (value & 0x0F);
}

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

    uint8_t sec = Wire.read();
    uint8_t min = Wire.read();
    uint8_t hrs = Wire.read();

    Wire.read(); // dzień tygodnia

    uint8_t date = Wire.read();
    uint8_t mon  = Wire.read();
    uint8_t yr   = Wire.read();

    dateTime.second = bcdToDec(sec & 0x7F);
    dateTime.minute = bcdToDec(min & 0x7F);
    dateTime.hour   = bcdToDec(hrs & 0x3F);

    dateTime.day   = bcdToDec(date & 0x3F);
    dateTime.month = bcdToDec(mon & 0x1F);
    dateTime.year  = 2000 + bcdToDec(yr);

    return true;
}

uint8_t RtcManager::daysInMonth(
    uint16_t year,
    uint8_t month) const
{
    const uint8_t days[] =
    {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

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
