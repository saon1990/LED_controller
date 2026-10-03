#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>

// ============================================================
// ESP32-S3 / WS2814
// ============================================================

#define LED_DATA_PIN 1
#define LED_COUNT    70

// ============================================================
// Efekt
// ============================================================

#define FRAME_TIME   20
#define MOVE_SPEED   0.35f
#define FADE_WIDTH   8.0f

Adafruit_NeoPixel strip(
    LED_COUNT,
    LED_DATA_PIN,
    NEO_GRBW + NEO_KHZ800
);

// ============================================================
// I2C / DS3231 / AT24C32
// ============================================================

#define I2C_SDA 8
#define I2C_SCL 9

#define RTC_ADDR    0x68
#define EEPROM_ADDR 0x57

#define EEPROM_TIMESTAMP_ADDR 0x0010
#define EEPROM_LED_STATE_ADDR 0x0014
#define EEPROM_LED_VALID_ADDR 0x0015

#define LED_VALID_MARKER 0xA5

// Heartbeat co 60 sekund
#define SAVE_INTERVAL 60UL

// >= 300 s = długi zanik
// < 300 s  = krótki zanik
#define LONG_OUTAGE 300UL

// ============================================================
// Stan LED
// ============================================================

bool ledState = false;
unsigned long lastSaveMillis = 0;

// ============================================================
// KOLOR RGBW
// Fizyczna kolejność WS2814 w tej taśmie: R-W-G-B
// ============================================================

uint32_t makeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    return strip.Color(r, w, g, b);
}

// ============================================================
// SMOOTHSTEP
// ============================================================

float smoothStep(float x)
{
    if (x <= 0.0f)
        return 0.0f;

    if (x >= 1.0f)
        return 1.0f;

    return x * x * (3.0f - 2.0f * x);
}

// ============================================================
// EFEKT ON
// ============================================================

void effectOn()
{
    float front = 0.0f;

    while (front < LED_COUNT + FADE_WIDTH)
    {
        for (int i = 0; i < LED_COUNT; i++)
        {
            float distance = front - (float)i;
            float brightness;

            if (distance <= 0.0f)
            {
                brightness = 0.0f;
            }
            else if (distance >= FADE_WIDTH)
            {
                brightness = 1.0f;
            }
            else
            {
                brightness = smoothStep(distance / FADE_WIDTH);
            }

            uint8_t level = (uint8_t)(brightness * 255.0f);

            strip.setPixelColor(
                i,
                makeColor(0, 0, 0, level)
            );
        }

        strip.show();

        front += MOVE_SPEED;
        delay(FRAME_TIME);
    }

    // Gwarantujemy pełną jasność
    for (int i = 0; i < LED_COUNT; i++)
    {
        strip.setPixelColor(
            i,
            makeColor(0, 0, 0, 255)
        );
    }

    strip.show();
}

// ============================================================
// EFEKT OFF
// Płynne wygaszanie od końca do początku.
// ============================================================

void effectOff()
{
    float front = LED_COUNT - 1;

    while (front > -FADE_WIDTH)
    {
        for (int i = 0; i < LED_COUNT; i++)
        {
            float distance = front - (float)i;
            float brightness;

            if (distance <= 0.0f)
            {
                brightness = 0.0f;
            }
            else if (distance >= FADE_WIDTH)
            {
                brightness = 1.0f;
            }
            else
            {
                brightness = smoothStep(distance / FADE_WIDTH);
            }

            uint8_t level = (uint8_t)(brightness * 255.0f);

            strip.setPixelColor(
                i,
                makeColor(0, 0, 0, level)
            );
        }

        strip.show();

        front -= MOVE_SPEED;
        delay(FRAME_TIME);
    }

    strip.clear();
    strip.show();
}

// ============================================================
// BCD -> DEC
// ============================================================

uint8_t bcdToDec(uint8_t value)
{
    return ((value >> 4) * 10) + (value & 0x0F);
}

// ============================================================
// ODCZYT DS3231
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

    if (Wire.requestFrom((uint8_t)RTC_ADDR, (uint8_t)7) != 7)
        return false;

    uint8_t sec = Wire.read();
    uint8_t min = Wire.read();
    uint8_t hrs = Wire.read();

    Wire.read(); // dzień tygodnia

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
// DNI W MIESIĄCU
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
// DATA/CZAS -> TIMESTAMP
// Sekundy od 2000-01-01 00:00:00
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
    {
        days += daysInMonth(year, m);
    }

    days += day - 1;

    return
        days * 86400UL +
        (uint32_t)hour * 3600UL +
        (uint32_t)minute * 60UL +
        second;
}

// ============================================================
// AT24C32 WRITE
// ============================================================

bool eepromWrite(
    uint16_t address,
    const uint8_t* data,
    uint8_t length
)
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

// ============================================================
// AT24C32 READ
// ============================================================

bool eepromRead(
    uint16_t address,
    uint8_t* data,
    uint8_t length
)
{
    Wire.beginTransmission(EEPROM_ADDR);

    Wire.write((address >> 8) & 0xFF);
    Wire.write(address & 0xFF);

    if (Wire.endTransmission(false) != 0)
        return false;

    if (Wire.requestFrom((uint8_t)EEPROM_ADDR, length) != length)
        return false;

    for (uint8_t i = 0; i < length; i++)
        data[i] = Wire.read();

    return true;
}

// ============================================================
// ODCZYT TIMESTAMPU
// ============================================================

bool readSavedTimestamp(uint32_t& timestamp)
{
    uint8_t buffer[4];

    if (!eepromRead(
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

// ============================================================
// ODCZYT ZAPISANEGO STANU LED
// ============================================================

bool readSavedLedState(bool& state)
{
    uint8_t valid;
    uint8_t value;

    if (!eepromRead(
            EEPROM_LED_VALID_ADDR,
            &valid,
            1))
    {
        return false;
    }

    if (valid != LED_VALID_MARKER)
        return false;

    if (!eepromRead(
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

// ============================================================
// ZAPIS STANU LED
// ============================================================

bool saveLedState(bool state)
{
    uint8_t value = state ? 0x01 : 0x00;

    if (!eepromWrite(
            EEPROM_LED_STATE_ADDR,
            &value,
            1))
    {
        return false;
    }

    uint8_t marker = LED_VALID_MARKER;

    if (!eepromWrite(
            EEPROM_LED_VALID_ADDR,
            &marker,
            1))
    {
        return false;
    }

    return true;
}

// ============================================================
// ZAPIS TIMESTAMPU
// ============================================================

bool saveTimestamp(uint32_t timestamp)
{
    uint8_t buffer[4];

    buffer[0] = (timestamp >> 24) & 0xFF;
    buffer[1] = (timestamp >> 16) & 0xFF;
    buffer[2] = (timestamp >> 8) & 0xFF;
    buffer[3] = timestamp & 0xFF;

    return eepromWrite(
        EEPROM_TIMESTAMP_ADDR,
        buffer,
        4
    );
}

// ============================================================
// ZAPIS KOMPLETNEGO STANU
// ============================================================

bool saveCurrentState()
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
        Serial.println("[EEPROM] BLAD ODCZYTU RTC");
        return false;
    }

    uint32_t timestamp =
        dateTimeToTimestamp(
            year,
            month,
            day,
            hour,
            minute,
            second
        );

    if (!saveTimestamp(timestamp))
    {
        Serial.println("[EEPROM] BLAD ZAPISU TIMESTAMPU");
        return false;
    }

    if (!saveLedState(ledState))
    {
        Serial.println("[EEPROM] BLAD ZAPISU STANU LED");
        return false;
    }

    Serial.printf(
        "[EEPROM] Heartbeat: %04d-%02d-%02d %02d:%02d:%02d | "
        "timestamp=%lu | LED=%s\n",
        year,
        month,
        day,
        hour,
        minute,
        second,
        timestamp,
        ledState ? "ON" : "OFF"
    );

    return true;
}

// ============================================================
// SPRAWDZENIE ZANIKU ZASILANIA
// ============================================================

void checkPowerLoss()
{
    uint32_t savedTimestamp;

    if (!readSavedTimestamp(savedTimestamp))
    {
        Serial.println("EEPROM: nie mozna odczytac timestampu.");
        Serial.println("Pierwsze/normalne uruchomienie.");
        ledState = false;
        return;
    }

    if (savedTimestamp == 0 ||
        savedTimestamp == 0xFFFFFFFF)
    {
        Serial.println("EEPROM: brak zapisanego timestampu.");
        Serial.println("Pierwsze uruchomienie.");
        ledState = false;
        return;
    }

    bool savedLedState;
    bool ledStateValid =
        readSavedLedState(savedLedState);

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
        Serial.println("BLAD: Nie mozna odczytac DS3231!");
        ledState = false;
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
    Serial.println("========================================");
    Serial.println("         POWER LOSS CHECK");
    Serial.println("========================================");

    Serial.printf(
        "Aktualny RTC:       %04d-%02d-%02d %02d:%02d:%02d\n",
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
        Serial.println("Zapisany stan LED:   BRAK");
    }

    if (currentTimestamp < savedTimestamp)
    {
        Serial.println("BLAD: timestamp EEPROM jest z przyszlosci.");
        ledState = false;
        return;
    }

    uint32_t difference =
        currentTimestamp - savedTimestamp;

    uint32_t hours = difference / 3600;
    uint32_t minutes = (difference % 3600) / 60;
    uint32_t seconds = difference % 60;

    Serial.println("----------------------------------------");
    Serial.printf(
        "Czas od ostatniego heartbeat: %lu sekund\n",
        difference
    );
    Serial.printf(
        "Czyli: %lu h %lu min %lu s\n",
        hours,
        minutes,
        seconds
    );
    Serial.println("----------------------------------------");

    if (difference >= LONG_OUTAGE)
    {
        ledState = false;

        Serial.println("WYNIK: DLUGI ZANIK ZASILANIA");
        Serial.println("LED -> OFF");
    }
    else
    {
        Serial.println("WYNIK: KROTKI ZANIK ZASILANIA");

        if (!ledStateValid)
        {
            ledState = false;
            Serial.println("Brak zapisanego stanu LED.");
            Serial.println("LED -> OFF");
        }
        else
        {
            ledState = savedLedState;

            Serial.printf(
                "Poprzedni stan LED: %s\n",
                savedLedState ? "ON" : "OFF"
            );

            Serial.printf(
                "LED -> %s\n",
                ledState ? "ON" : "OFF"
            );
        }
    }

    Serial.println();
}

// ============================================================
// USTAWIENIE FIZYCZNEGO STANU LED PO BOOT
// ============================================================

void applyLedState()
{
    if (ledState)
    {
        Serial.println("Uruchamiam efekt ON...");
        effectOn();
    }
    else
    {
        strip.clear();
        strip.show();
        Serial.println("LED -> OFF");
    }
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    strip.begin();
    strip.setBrightness(255);
    strip.clear();
    strip.show();

    Wire.begin(I2C_SDA, I2C_SCL);

    Serial.println();
    Serial.println("========================================");
    Serial.println("       LED CONTROLLER - START");
    Serial.println("========================================");

    // Najpierw sprawdzamy, jak długo trwał zanik.
    // Jeszcze NIC nie zapisujemy.
    checkPowerLoss();

    Serial.println();
    Serial.printf(
        "Stan po sprawdzeniu zaniku: %s\n",
        ledState ? "ON" : "OFF"
    );

    // Przywracamy fizyczny stan LED.
    applyLedState();

    // Pierwszy heartbeat dopiero po 60 sekundach.
    lastSaveMillis = millis();

    Serial.println();
    Serial.println("Normalna praca uruchomiona.");
    Serial.println("Heartbeat EEPROM: co 60 sekund.");
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    // ========================================================
    // HEARTBEAT CO 60 SEKUND
    // ========================================================

    if (millis() - lastSaveMillis >=
        SAVE_INTERVAL * 1000UL)
    {
        lastSaveMillis = millis();
        saveCurrentState();
    }

    // ========================================================
    // NA RAZIE TESTOWO NIC WIĘCEJ.
    //
    // W następnym etapie tutaj dojdzie:
    // - sterowanie ON/OFF
    // - Wi-Fi
    // - Matter / Google Home
    // ========================================================

    delay(10);
}
