#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
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
// WIFI
// ============================================================

const char* WIFI_SSID     = "Orange_Internet_B510";
const char* WIFI_PASSWORD = "bulkaimaslo12345.";

WebServer server(80);

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

#define SAVE_INTERVAL 60UL
#define LONG_OUTAGE   300UL

// ============================================================
// Stan LED
// ============================================================

bool ledState = false;
unsigned long lastSaveMillis = 0;

// ============================================================
// KOLOR RGBW
// Fizyczna kolejność: R-W-G-B
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
                brightness = 0.0f;
            else if (distance >= FADE_WIDTH)
                brightness = 1.0f;
            else
                brightness = smoothStep(distance / FADE_WIDTH);

            uint8_t level =
                (uint8_t)(brightness * 255.0f);

            strip.setPixelColor(
                i,
                makeColor(0, 0, 0, level)
            );
        }

        strip.show();

        front += MOVE_SPEED;
        delay(FRAME_TIME);
    }

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
                brightness = 0.0f;
            else if (distance >= FADE_WIDTH)
                brightness = 1.0f;
            else
                brightness = smoothStep(distance / FADE_WIDTH);

            uint8_t level =
                (uint8_t)(brightness * 255.0f);

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
// EEPROM WRITE
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
// EEPROM READ
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

// ============================================================
// READ SAVED TIMESTAMP
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
// READ SAVED LED STATE
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
// SAVE LED STATE
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
// SAVE TIMESTAMP
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
// SAVE CURRENT STATE
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
        Serial.println(
            "[EEPROM] BLAD ODCZYTU RTC"
        );

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
        Serial.println(
            "[EEPROM] BLAD ZAPISU TIMESTAMPU"
        );

        return false;
    }

    if (!saveLedState(ledState))
    {
        Serial.println(
            "[EEPROM] BLAD ZAPISU STANU LED"
        );

        return false;
    }

    Serial.printf(
        "[EEPROM] Heartbeat: "
        "%04d-%02d-%02d %02d:%02d:%02d "
        "| timestamp=%lu | LED=%s\n",
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
// POWER LOSS CHECK
// ============================================================

void checkPowerLoss()
{
    uint32_t savedTimestamp;

    if (!readSavedTimestamp(savedTimestamp))
    {
        Serial.println(
            "EEPROM: nie mozna odczytac timestampu."
        );

        Serial.println(
            "Pierwsze/normalne uruchomienie."
        );

        ledState = false;
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
        Serial.println(
            "BLAD: Nie mozna odczytac DS3231!"
        );

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

        ledState = false;
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
        ledState = false;

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
            ledState = false;

            Serial.println(
                "Brak zapisanego stanu LED."
            );

            Serial.println(
                "LED -> OFF"
            );
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
// USTAWIENIE LED ON
// ============================================================

void setLedOn()
{
    if (ledState)
    {
        Serial.println(
            "[LED] ON - stan juz jest ON"
        );

        return;
    }

    Serial.println(
        "[LED] ON - start efektu"
    );

    ledState = true;

    // Zapisujemy stan natychmiast.
    saveCurrentState();

    // Dopiero potem wykonujemy efekt.
    effectOn();

    Serial.println(
        "[LED] ON - efekt zakonczony"
    );
}

// ============================================================
// USTAWIENIE LED OFF
// ============================================================

void setLedOff()
{
    if (!ledState)
    {
        Serial.println(
            "[LED] OFF - stan juz jest OFF"
        );

        strip.clear();
        strip.show();

        return;
    }

    Serial.println(
        "[LED] OFF - start efektu"
    );

    // Stan docelowy zapisujemy od razu.
    ledState = false;

    saveCurrentState();

    // Plynne wygaszenie.
    effectOff();

    Serial.println(
        "[LED] OFF - efekt zakonczony"
    );
}

// ============================================================
// STRONA WWW
// ============================================================

String makeWebPage()
{
    String stateText =
        ledState ? "WŁĄCZONE" : "WYŁĄCZONE";

    String stateClass =
        ledState ? "on" : "off";

    String buttons;

    if (ledState)
    {
        buttons =
            "<a class='button off' "
            "href='/off'>WYŁĄCZ</a>";
    }
    else
    {
        buttons =
            "<a class='button on' "
            "href='/on'>WŁĄCZ</a>";
    }

    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="UTF-8">
<meta name="viewport"
      content="width=device-width, initial-scale=1">

<meta http-equiv="refresh"
      content="10">

<title>LED Terrace</title>

<style>
body {
    margin: 0;
    background: #111;
    color: #fff;
    font-family: Arial, sans-serif;
    text-align: center;
}

.container {
    max-width: 420px;
    margin: 50px auto;
    padding: 30px 20px;
}

h1 {
    font-size: 30px;
    margin-bottom: 35px;
}

.status {
    font-size: 22px;
    margin-bottom: 30px;
}

.on {
    color: #35d07f;
}

.off {
    color: #999;
}

.button {
    display: block;
    padding: 20px;
    margin: 15px 0;
    border-radius: 12px;
    text-decoration: none;
    font-size: 24px;
    font-weight: bold;
}

.button.on {
    background: #16834b;
    color: white;
}

.button.off {
    background: #a52a2a;
    color: white;
}
</style>
</head>

<body>

<div class="container">

<h1>LED TERRACE</h1>

<div class="status">
Stan:
<strong class="STATE_CLASS">
STATE_TEXT
</strong>
</div>

BUTTONS

</div>

</body>
</html>
)rawliteral";

    html.replace(
        "STATE_CLASS",
        stateClass
    );

    html.replace(
        "STATE_TEXT",
        stateText
    );

    html.replace(
        "BUTTONS",
        buttons
    );

    return html;
}

// ============================================================
// HTTP HANDLERS
// ============================================================

void handleRoot()
{
    server.send(
        200,
        "text/html; charset=utf-8",
        makeWebPage()
    );
}

void handleOn()
{
    server.send(
        200,
        "text/html; charset=utf-8",
        "<html><body>"
        "<h2>Włączam LED...</h2>"
        "<a href='/'>Powrót</a>"
        "</body></html>"
    );

    // Efekt wykonujemy po wysłaniu odpowiedzi.
    setLedOn();
}

void handleOff()
{
    server.send(
        200,
        "text/html; charset=utf-8",
        "<html><body>"
        "<h2>Wyłączam LED...</h2>"
        "<a href='/'>Powrót</a>"
        "</body></html>"
    );

    setLedOff();
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
        Serial.println(
            "BLAD: Nie udalo sie polaczyc z WiFi."
        );

        return false;
    }

    Serial.println("WiFi OK");

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    return true;
}

// ============================================================
// WEB SERVER
// ============================================================

void startWebServer()
{
    server.on("/", HTTP_GET, handleRoot);

    server.on("/on", HTTP_GET, handleOn);

    server.on("/off", HTTP_GET, handleOff);

    server.begin();

    Serial.println(
        "WEB SERVER STARTED"
    );

    Serial.print(
        "Open: http://"
    );

    Serial.println(
        WiFi.localIP()
    );
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
        ledState ? "ON" : "OFF"
    );

    // --------------------------------------------------------
    // Przywrócenie stanu po krótkim zaniku
    // --------------------------------------------------------

    if (ledState)
    {
        Serial.println(
            "Przywracam LED -> ON"
        );

        effectOn();
    }
    else
    {
        strip.clear();
        strip.show();

        Serial.println(
            "LED -> OFF"
        );
    }

    // --------------------------------------------------------
    // WIFI
    // --------------------------------------------------------

    if (connectWiFi())
    {
        startWebServer();
    }

    // --------------------------------------------------------
    // HEARTBEAT
    // --------------------------------------------------------

    lastSaveMillis = millis();

    Serial.println();

    Serial.println(
        "Normalna praca uruchomiona."
    );

    Serial.println(
        "Heartbeat EEPROM: co 60 sekund."
    );
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    server.handleClient();

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
