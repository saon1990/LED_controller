
#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "LED/LedController.h"
#include "RTC/RtcManager.h"
#include "STORAGE/EepromManager.h"

// ============================================================
// GLOBAL OBJECTS
// ============================================================

LedController led;
RtcManager rtc;
EepromManager storage;

// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

void setLedOn();
void setLedOff();
void setLedFlow();

bool saveCurrentState();
void checkPowerLoss();
void handleSerialCommands();

unsigned long lastSaveMillis = 0;

// ============================================================
// LED ACTIONS
// ============================================================

// ============================================================
// STATIC WHITE ON
// ============================================================

void setLedOn()
{
    // Jeżeli STATIC WHITE jest już aktywny,
    // nie wykonujemy żadnej dodatkowej operacji.

    if (led.getMode() == LedMode::STATIC_WHITE)
    {
        Serial.println(
            "[LED] ON - STATIC WHITE juz aktywny."
        );

        return;
    }

    Serial.println(
        "[LED] ON - NATYCHMIASTOWE STATIC WHITE"
    );

    // Natychmiast przerywamy FLOW i zapalamy
    // całą taśmę na biało.

    led.turnOn();

    if (!saveCurrentState())
    {
        Serial.println(
            "[LED] UWAGA: nie zapisano trybu."
        );
    }
}

// ============================================================
// FLOW ON
// ============================================================

void setLedFlow()
{
    if (led.getMode() == LedMode::FLOW)
    {
        Serial.println(
            "[LED] FLOW - efekt juz jest aktywny."
        );

        return;
    }

    Serial.println(
        "[LED] FLOW - START"
    );

    led.startFlow();

    if (!saveCurrentState())
    {
        Serial.println(
            "[LED] UWAGA: nie zapisano trybu FLOW."
        );
    }
}

// ============================================================
// LED OFF
// ============================================================

void setLedOff()
{
    if (!led.isOn())
    {
        Serial.println(
            "[LED] OFF - LED juz jest wylaczony."
        );

        return;
    }

    Serial.println(
        "[LED] OFF - STOP"
    );

    led.turnOff();

    // Zapisujemy OFF od razu po otrzymaniu polecenia.
    // Jeżeli zasilanie zniknie podczas animacji wygaszania,
    // po ponownym uruchomieniu LED ma pozostać OFF.

    if (!saveCurrentState())
    {
        Serial.println(
            "[LED] UWAGA: nie zapisano trybu OFF."
        );
    }
}

// ============================================================
// SERIAL COMMAND HANDLER
// ============================================================

void handleSerialCommands()
{
    while (Serial.available() > 0)
    {
        char command = Serial.read();

        // Ignorujemy znaki końca linii.

        if (command == '\r' || command == '\n')
            continue;

        Serial.printf(
            "[SERIAL] Otrzymano komende: %c\n",
            command
        );

        switch (command)
        {
            case '1':

                setLedOn();

                break;

            case '2':

                setLedFlow();

                break;

            case '0':

                setLedOff();

                break;

            default:

                Serial.println(
                    "[SERIAL] Nieznana komenda."
                );

                Serial.println(
                    "1 = STATIC WHITE"
                );

                Serial.println(
                    "2 = FLOW"
                );

                Serial.println(
                    "0 = OFF"
                );

                break;
        }
    }
}

// ============================================================
// SAVE CURRENT STATE
// ============================================================

bool saveCurrentState()
{
    RtcDateTime now;

    if (!rtc.getDateTime(now))
    {
        Serial.println(
            "[STORAGE] BLAD ODCZYTU RTC."
        );

        return false;
    }

    uint32_t timestamp = rtc.toTimestamp(now);

    uint8_t mode =
        static_cast<uint8_t>(led.getMode());

    // --------------------------------------------------------
    // SAVE TIMESTAMP
    // --------------------------------------------------------

    if (!storage.saveTimestamp(timestamp))
    {
        Serial.println(
            "[STORAGE] BLAD ZAPISU TIMESTAMP."
        );

        return false;
    }

    // --------------------------------------------------------
    // SAVE LED MODE
    // --------------------------------------------------------

    if (!storage.saveLedMode(mode))
    {
        Serial.println(
            "[STORAGE] BLAD ZAPISU TRYBU LED."
        );

        return false;
    }

    Serial.printf(
        "[STORAGE] Zapisano: "
        "%04u-%02u-%02u %02u:%02u:%02u "
        "| timestamp=%lu | mode=0x%02X\n",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second,
        timestamp,
        mode
    );

    return true;
}

// ============================================================
// POWER LOSS CHECK
// ============================================================

void checkPowerLoss()
{
    uint32_t savedTimestamp = 0;
    uint8_t savedMode = 0xFF;

    // --------------------------------------------------------
    // LOAD TIMESTAMP
    // --------------------------------------------------------

    if (!storage.loadTimestamp(savedTimestamp))
    {
        Serial.println(
            "[POWER] BLAD ODCZYTU TIMESTAMP."
        );

        Serial.println(
            "[POWER] LED -> OFF"
        );

        led.restoreMode(LedMode::OFF);

        saveCurrentState();

        return;
    }

    if (
        savedTimestamp == 0 ||
        savedTimestamp == 0xFFFFFFFF
    )
    {
        Serial.println(
            "[POWER] BRAK POPRAWNEGO TIMESTAMP."
        );

        Serial.println(
            "[POWER] LED -> OFF"
        );

        led.restoreMode(LedMode::OFF);

        saveCurrentState();

        return;
    }

    // --------------------------------------------------------
    // LOAD LED MODE
    // --------------------------------------------------------

    bool modeValid =
        storage.loadLedMode(savedMode);

    // --------------------------------------------------------
    // READ RTC
    // --------------------------------------------------------

    RtcDateTime now;

    if (!rtc.getDateTime(now))
    {
        Serial.println(
            "[POWER] BLAD ODCZYTU DS3231."
        );

        Serial.println(
            "[POWER] LED -> OFF"
        );

        led.restoreMode(LedMode::OFF);

        return;
    }

    uint32_t currentTimestamp =
        rtc.toTimestamp(now);

    // --------------------------------------------------------
    // DIAGNOSTICS
    // --------------------------------------------------------

    Serial.println();

    Serial.println(
        "========================================"
    );

    Serial.println(
        "             POWER LOSS CHECK"
    );

    Serial.println(
        "========================================"
    );

    Serial.printf(
        "Aktualny RTC:       "
        "%04u-%02u-%02u %02u:%02u:%02u\n",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second
    );

    Serial.printf(
        "Aktualny timestamp: %lu\n",
        currentTimestamp
    );

    Serial.printf(
        "Zapisany timestamp: %lu\n",
        savedTimestamp
    );

    if (modeValid)
    {
        Serial.printf(
            "Zapisany tryb LED: 0x%02X\n",
            savedMode
        );
    }
    else
    {
        Serial.println(
            "Zapisany tryb LED: BRAK / BLAD"
        );
    }

    // --------------------------------------------------------
    // TIMESTAMP VALIDATION
    // --------------------------------------------------------

    if (currentTimestamp < savedTimestamp)
    {
        Serial.println(
            "[POWER] BLAD: RTC jest wczesniejszy "
            "niz zapisany timestamp."
        );

        Serial.println(
            "[POWER] LED -> OFF"
        );

        led.restoreMode(LedMode::OFF);

        saveCurrentState();

        return;
    }

    // --------------------------------------------------------
    // OUTAGE DURATION
    // --------------------------------------------------------

    uint32_t difference =
        currentTimestamp - savedTimestamp;

    Serial.printf(
        "Czas od ostatniego heartbeat: %lu s\n",
        difference
    );

    // --------------------------------------------------------
    // LONG OUTAGE
    // --------------------------------------------------------

    if (difference >= LONG_OUTAGE)
    {
        Serial.println(
            "[POWER] DLUGI ZANIK ZASILANIA."
        );

        Serial.printf(
            "[POWER] Prog: %lu s\n",
            LONG_OUTAGE
        );

        led.restoreMode(LedMode::OFF);

        Serial.println(
            "[POWER] LED -> OFF"
        );

        // Zapisujemy OFF i aktualny timestamp.
        // Dzięki temu następny restart nie przywróci
        // poprzedniego trybu.

        saveCurrentState();

        return;
    }

    // --------------------------------------------------------
    // SHORT OUTAGE
    // --------------------------------------------------------

    Serial.println(
        "[POWER] KROTKI ZANIK ZASILANIA."
    );

    if (!modeValid)
    {
        Serial.println(
            "[POWER] Niepoprawny zapis trybu LED."
        );

        Serial.println(
            "[POWER] LED -> OFF"
        );

        led.restoreMode(LedMode::OFF);

        saveCurrentState();

        return;
    }

    // --------------------------------------------------------
    // RESTORE MODE
    // --------------------------------------------------------

    switch (static_cast<LedMode>(savedMode))
    {
        case LedMode::OFF:

            Serial.println(
                "[POWER] Przywracam OFF."
            );

            led.restoreMode(LedMode::OFF);

            break;

        case LedMode::STATIC_WHITE:

            Serial.println(
                "[POWER] Przywracam STATIC WHITE."
            );

            led.restoreMode(LedMode::STATIC_WHITE);

            break;

        case LedMode::FLOW:

            Serial.println(
                "[POWER] Przywracam FLOW od poczatku."
            );

            led.restoreMode(LedMode::FLOW);

            break;

        default:

            Serial.println(
                "[POWER] Nieznany tryb LED."
            );

            Serial.println(
                "[POWER] LED -> OFF"
            );

            led.restoreMode(LedMode::OFF);

            saveCurrentState();

            break;
    }

    Serial.println(
        "========================================"
    );

    Serial.println();
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);

    // --------------------------------------------------------
    // LED INIT
    // --------------------------------------------------------

    led.begin();

    // --------------------------------------------------------
    // I2C INIT
    // --------------------------------------------------------

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
    // RTC INIT
    // --------------------------------------------------------

    bool rtcReady = rtc.begin();

    // --------------------------------------------------------
    // EEPROM INIT
    // --------------------------------------------------------

    bool storageReady = storage.begin();

    // --------------------------------------------------------
    // POWER LOSS CHECK
    // --------------------------------------------------------

    if (rtcReady && storageReady)
    {
        checkPowerLoss();
    }
    else
    {
        Serial.println(
            "[START] RTC lub EEPROM niedostepne."
        );

        Serial.println(
            "[START] LED -> OFF"
        );

        led.restoreMode(LedMode::OFF);
    }

    // --------------------------------------------------------
    // RESTORED MODE
    // --------------------------------------------------------

    Serial.printf(
        "[START] Aktualny tryb LED: 0x%02X\n",
        static_cast<uint8_t>(led.getMode())
    );

    // --------------------------------------------------------
    // HEARTBEAT INIT
    // --------------------------------------------------------

    lastSaveMillis = millis();

    Serial.println();

    Serial.println(
        "Normalna praca uruchomiona."
    );

    Serial.printf(
        "Heartbeat STORAGE: co %lu sekund.\n",
        SAVE_INTERVAL
    );

    Serial.printf(
        "Limit zaniku zasilania: %lu sekund.\n",
        LONG_OUTAGE
    );

    Serial.println();

    Serial.println(
        "STEROWANIE SERIAL:"
    );

    Serial.println(
        "1 = STATIC WHITE"
    );

    Serial.println(
        "2 = FLOW"
    );

    Serial.println(
        "0 = OFF"
    );
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    // --------------------------------------------------------
    // SERIAL COMMANDS
    // --------------------------------------------------------

    handleSerialCommands();

    // --------------------------------------------------------
    // LED ANIMATION
    // --------------------------------------------------------

    led.update();

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
