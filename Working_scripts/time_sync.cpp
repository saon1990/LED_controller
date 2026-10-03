#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>

// ============================================================
// WIFI
// ============================================================

const char* WIFI_SSID     = "Orange_Internet_B510";
const char* WIFI_PASSWORD = "bulkaimaslo12345.";

// ============================================================
// I2C / DS3231
// ============================================================

#define I2C_SDA 8
#define I2C_SCL 9

#define RTC_ADDR 0x68

// Polska:
// CET  = UTC+1
// CEST = UTC+2
//
// Reguła automatycznie uwzględnia zmianę czasu letni/zimowy.
const char* TIME_ZONE = "CET-1CEST,M3.5.0,M10.5.0/3";

// ============================================================
// BCD
// ============================================================

uint8_t decToBcd(uint8_t value)
{
    return ((value / 10) << 4) | (value % 10);
}

uint8_t bcdToDec(uint8_t value)
{
    return ((value >> 4) * 10) + (value & 0x0F);
}

// ============================================================
// DS3231 - ZAPIS CZASU
// ============================================================

bool rtcSetTime(
    const tm& timeinfo
)
{
    Wire.beginTransmission(RTC_ADDR);

    // Rejestr sekund
    Wire.write(0x00);

    Wire.write(decToBcd(timeinfo.tm_sec));
    Wire.write(decToBcd(timeinfo.tm_min));
    Wire.write(decToBcd(timeinfo.tm_hour));

    // Dzień tygodnia
    uint8_t weekday = timeinfo.tm_wday;
    if (weekday == 0)
        weekday = 7;

    Wire.write(decToBcd(weekday));

    Wire.write(decToBcd(timeinfo.tm_mday));
    Wire.write(decToBcd(timeinfo.tm_mon + 1));

    uint8_t year = (timeinfo.tm_year + 1900) - 2000;

    Wire.write(decToBcd(year));

    return Wire.endTransmission() == 0;
}

// ============================================================
// DS3231 - ODCZYT CZASU
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

    Wire.read(); // weekday

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
// WYŚWIETLENIE CZASU DS3231
// ============================================================

void printRTC()
{
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
        Serial.println("BLAD odczytu DS3231!");
        return;
    }

    Serial.printf(
        "DS3231: %04d-%02d-%02d %02d:%02d:%02d\n",
        year,
        month,
        day,
        hour,
        minute,
        second
    );
}

// ============================================================
// WIFI
// ============================================================

bool connectWiFi()
{
    Serial.println();
    Serial.println("Laczenie z WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    uint32_t start = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < 20000
    )
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("BLAD: Nie udalo sie polaczyc z WiFi.");
        return false;
    }

    Serial.println("WiFi OK");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    return true;
}

// ============================================================
// NTP
// ============================================================

bool syncNTP()
{
    Serial.println();
    Serial.println("Synchronizacja czasu z NTP...");

    configTzTime(
        TIME_ZONE,
        "pool.ntp.org",
        "time.nist.gov",
        "time.google.com"
    );

    struct tm timeinfo;

    uint32_t start = millis();

    while (
        !getLocalTime(&timeinfo) &&
        millis() - start < 20000
    )
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();

    if (!getLocalTime(&timeinfo))
    {
        Serial.println("BLAD: NTP nie zwrocil czasu.");
        return false;
    }

    Serial.println("NTP OK");

    Serial.printf(
        "Czas NTP: %04d-%02d-%02d %02d:%02d:%02d\n",
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
    );

    // --------------------------------------------------------
    // Zapis NTP -> DS3231
    // --------------------------------------------------------

    if (!rtcSetTime(timeinfo))
    {
        Serial.println("BLAD zapisu czasu do DS3231!");
        return false;
    }

    Serial.println("Czas zapisany do DS3231.");

    return true;
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("       DS3231 NTP SYNC TEST");
    Serial.println("========================================");

    // --------------------------------------------------------
    // I2C
    // --------------------------------------------------------

    Wire.begin(
        I2C_SDA,
        I2C_SCL
    );

    Serial.println();
    Serial.println("Aktualny czas DS3231 PRZED synchronizacja:");

    printRTC();

    // --------------------------------------------------------
    // WIFI
    // --------------------------------------------------------

    if (!connectWiFi())
        return;

    // --------------------------------------------------------
    // NTP -> DS3231
    // --------------------------------------------------------

    if (!syncNTP())
        return;

    // --------------------------------------------------------
    // TEST
    // --------------------------------------------------------

    Serial.println();
    Serial.println("========================================");
    Serial.println("       TEST DS3231");
    Serial.println("========================================");

    for (int i = 0; i < 10; i++)
    {
        printRTC();
        delay(1000);
    }

    Serial.println();
    Serial.println("TEST ZAKONCZONY.");
    Serial.println("Mozesz odlaczyc ESP32 od USB.");
}

void loop()
{
}