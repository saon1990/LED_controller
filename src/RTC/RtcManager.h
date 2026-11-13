
#pragma once

#include <Arduino.h>

struct RtcDateTime
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

class RtcManager
{
public:
    bool begin();

    bool getDateTime(RtcDateTime& dateTime);
    uint32_t toTimestamp(const RtcDateTime& dateTime) const;

private:
    uint8_t bcdToDec(uint8_t value) const;
    uint8_t decToBcd(uint8_t value) const;

    uint8_t daysInMonth(uint16_t year, uint8_t month) const;

    bool isDateTimeValid(const RtcDateTime& dateTime) const;
    bool isOscillatorStopped(bool& stopped);

    bool setDateTime(const RtcDateTime& dateTime);
    bool setTimeFromBuild();

    uint8_t monthFromString(const char* month) const;
};
