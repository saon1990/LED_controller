#include <Arduino.h>
#include <Wire.h>
#include <WebServer.h>
#include <WiFi.h>
#include "config.h"
#include "LED/LedController.h"
#include "RTC/RtcManager.h"
#include "STORAGE/EepromManager.h"
#include "WEB/WebServerManager.h"

// ============================================================
// LED CONTROLLER
// ============================================================

LedController led;
RtcManager rtc;
EepromManager storage;

// ============================================================
// GLOBALNE DEKLARACJE
// ============================================================

WebServerManager web;

void setLedOn();
void setLedOff();
bool saveCurrentState();

// ============================================================
// LED ACTION WRAPPERS
// ============================================================

void setLedOn()
{
    if (led.isOn())
    {
        Serial.println("[LED] ON - stan juz jest ON");
        return;
    }

    Serial.println("[LED] ON - start efektu");

    led.turnOn();

    if (!saveCurrentState())
        Serial.println("[LED] UWAGA: nie zapisano stanu.");

    Serial.println("[LED] ON - efekt zakonczony");
}

void setLedOff()
{
    if (!led.isOn())
    {
        Serial.println("[LED] OFF - stan juz jest OFF");
        led.clear();
        return;
    }

    Serial.println("[LED] OFF - start efektu");

    led.turnOff();

    if (!saveCurrentState())
        Serial.println("[LED] UWAGA: nie zapisano stanu.");

    Serial.println("[LED] OFF - efekt zakonczony");
}

// ============================================================
// STORAGE / RTC
// ============================================================



// ============================================================

unsigned long lastSaveMillis = 0;

// ============================================================
// BCD
// ============================================================

uint8_t bcdToDec(uint8_t value)
{
    return ((value >> 4) * 10) + (value & 0x0F);
}

// ============================================================
// RTC READ
// ============================================================

bool rtcRead(
    uint16_t& year,
    uint8_t& month,
    uint8_t& day,
    uint8_t& hour,
    uint8_t& minute,
    uint8_t& second
)
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

    Wire.read();

    uint8_t date = Wire.read();
    uint8_t mon  = Wire.read();
    uint8_t yr   = Wire.read();

    second = bcdToDec(sec & 0x7F);
    minute = bcdToDec(min & 0x7F);
    hour   = bcdToDec(hrs & 0x3F);

    day   = bcdToDec(date & 0x3F);
    month = bcdToDec(mon & 0x1F);
    year  = 2000 + bcdToDec(yr);

    return true;
}

// ============================================================
// DAYS IN MONTH
// ============================================================

uint8_t daysInMonth(uint16_t year, uint8_t month)
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

// ============================================================
// DATETIME -> TIMESTAMP
// ============================================================

uint32_t dateTimeToTimestamp(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
)
{
    uint32_t days = 0;

    for (uint16_t y = 2000; y < year; y++)
    {
        bool leap =
            ((y % 4 == 0) &&
             ((y % 100 != 0) ||
              (y % 400 == 0)));

        days += leap ? 366 : 365;
    }

    for (uint8_t m = 1; m < month; m++)
        days += daysInMonth(year, m);

    days += day - 1;

    return
        days * 86400UL +
        (uint32_t)hour * 3600UL +
        (uint32_t)minute * 60UL +
        second;
}

// ============================================================
// SAVE CURRENT STATE
// ============================================================

bool saveCurrentState()
{
    RtcDateTime now;

    if (!rtc.getDateTime(now))
    {
        Serial.println("[STORAGE] BLAD ODCZYTU RTC");
        return false;
    }

    uint32_t timestamp = rtc.toTimestamp(now);

    if (!storage.saveTimestamp(timestamp))
    {
        Serial.println(
            "[STORAGE] BLAD ZAPISU TIMESTAMPU"
        );

        return false;
    }

    if (!storage.saveLedState(led.isOn()))
    {
        Serial.println(
            "[STORAGE] BLAD ZAPISU STANU LED"
        );

        return false;
    }

    Serial.printf(
        "[STORAGE] Heartbeat: "
        "%04u-%02u-%02u %02u:%02u:%02u "
        "| timestamp=%lu | LED=%s\n",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second,
        timestamp,
        led.isOn() ? "ON" : "OFF"
    );

    return true;
}

// ============================================================
// POWER LOSS CHECK
// ============================================================

void checkPowerLoss()
{
    uint32_t savedTimestamp;

    if (!storage.loadTimestamp(savedTimestamp))
    {
        Serial.println(
            "EEPROM: nie mozna odczytac timestampu."
        );

        Serial.println(
            "Pierwsze/normalne uruchomienie."
        );

        led.restoreState(false);
        return;
    }

    if (savedTimestamp == 0 ||
        savedTimestamp == 0xFFFFFFFF)
    {
        Serial.println(
            "EEPROM: brak zapisanego timestampu."
        );

        Serial.println(
            "Pierwsze uruchomienie."
        );

        led.restoreState(false);
        return;
    }

    bool savedLedState;

    bool ledStateValid =
        storage.loadLedState(savedLedState);

    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;

    if (!rtcRead(
            year,
            month,
            day,
            hour,
            minute,
            second))
    {
        Serial.println(
            "BLAD: Nie mozna odczytac DS3231!"
        );

        led.restoreState(false);
        return;
    }

    uint32_t currentTimestamp =
        dateTimeToTimestamp(
            year,
            month,
            day,
            hour,
            minute,
            second
        );

    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "         POWER LOSS CHECK"
    );

    Serial.println(
        "========================================"
    );

    Serial.printf(
        "Aktualny RTC:       "
        "%04d-%02d-%02d %02d:%02d:%02d\n",
        year,
        month,
        day,
        hour,
        minute,
        second
    );

    Serial.printf(
        "Aktualny timestamp:  %lu\n",
        currentTimestamp
    );

    Serial.printf(
        "Zapisany timestamp:   %lu\n",
        savedTimestamp
    );

    if (ledStateValid)
    {
        Serial.printf(
            "Zapisany stan LED:   %s\n",
            savedLedState ? "ON" : "OFF"
        );
    }
    else
    {
        Serial.println(
            "Zapisany stan LED:   BRAK"
        );
    }

    if (currentTimestamp < savedTimestamp)
    {
        Serial.println(
            "BLAD: timestamp EEPROM "
            "jest z przyszlosci."
        );

        led.restoreState(false);
        return;
    }

    uint32_t difference =
        currentTimestamp -
        savedTimestamp;

    Serial.println(
        "----------------------------------------"
    );

    Serial.printf(
        "Czas od ostatniego heartbeat: "
        "%lu sekund\n",
        difference
    );

    Serial.println(
        "----------------------------------------"
    );

    if (difference >= LONG_OUTAGE)
    {
        led.turnOff();

        Serial.println(
            "WYNIK: DLUGI ZANIK ZASILANIA"
        );

        Serial.println(
            "LED -> OFF"
        );
    }
    else
    {
        Serial.println(
            "WYNIK: KROTKI ZANIK ZASILANIA"
        );

        if (!ledStateValid)
        {
            led.restoreState(false);

            Serial.println(
                "Brak zapisanego stanu LED."
            );

            Serial.println(
                "LED -> OFF"
            );
        }
        else
        {
            if (savedLedState) led.turnOn(); else led.turnOff();

            Serial.printf(
                "Poprzedni stan LED: %s\n",
                savedLedState ? "ON" : "OFF"
            );

            Serial.printf(
                "LED -> %s\n",
                led.isOn() ? "ON" : "OFF"
            );
        }
    }

    Serial.println();
}

// ============================================================
// WEB
// ============================================================

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);

    led.begin();

    Wire.begin(
        I2C_SDA,
        I2C_SCL
    );

    Serial.println();

    Serial.println(
        "========================================"
    );

    Serial.println(
        "       LED CONTROLLER - START"
    );

    Serial.println(
        "========================================"
    );

    // --------------------------------------------------------
    // POWER LOSS CHECK
    // --------------------------------------------------------

    checkPowerLoss();

    Serial.println();

    Serial.printf(
        "Stan po sprawdzeniu zaniku: %s\n",
        led.isOn() ? "ON" : "OFF"
    );

    // --------------------------------------------------------
    // Przywrócenie stanu po krótkim zaniku
    // --------------------------------------------------------

    if (led.isOn())
    {
        Serial.println(
            "Przywracam LED -> ON"
        );

        led.turnOn();
    }
    else
    {
        led.clear();

        Serial.println(
            "LED -> OFF"
        );
    }

    // --------------------------------------------------------
    // WEB SERVER
    // --------------------------------------------------------

    web.begin(
        led,
        setLedOn,
        setLedOff
    );

    // --------------------------------------------------------
    // NETWORK
    // --------------------------------------------------------
    // RainMaker zostal celowo usuniety.
    // WebServer pozostaje w projekcie, ale uruchomimy go
    // ponownie po wydzieleniu modulu WEB.
    // --------------------------------------------------------
    // HEARTBEAT
    // --------------------------------------------------------

    lastSaveMillis = millis();

    Serial.println();

    Serial.println(
        "Normalna praca uruchomiona."
    );

    Serial.println(
        "Heartbeat STORAGE: co 60 sekund."
    );
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    web.handleClient();

    // --------------------------------------------------------
    // HEARTBEAT
    // --------------------------------------------------------

    if (
        millis() - lastSaveMillis >=
        SAVE_INTERVAL * 1000UL
    )
    {
        lastSaveMillis = millis();

        saveCurrentState();
    }

    delay(2);
}
