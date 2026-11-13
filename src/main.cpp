#include <Arduino.h>
#include <Wire.h>

#include "config.h"

#include "LED/LedController.h"
#include "RTC/RtcManager.h"
#include "STORAGE/EepromManager.h"
#include "REMOTE/RemoteManager.h"

// ============================================================
// GLOBAL OBJECTS
// ============================================================

LedController led;
RtcManager rtc;
EepromManager storage;

REMOTE::RemoteManager remote;

// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

void setLedOn();
void setLedOff();
void setLedFlow();

bool saveCurrentState();
void checkPowerLoss();

void handleSerialCommands();
void handleRemoteCommands();

bool saveLastActiveMode(uint8_t mode);

unsigned long lastSaveMillis = 0;

// ============================================================
// SAVE LAST ACTIVE MODE
// ============================================================

bool saveLastActiveMode(uint8_t mode)
{
    if (
        mode != static_cast<uint8_t>(LedMode::STATIC_WHITE) &&
        mode != static_cast<uint8_t>(LedMode::FLOW)
    )
    {
        return false;
    }

    return storage.saveLastActiveLedMode(mode);
}

// ============================================================
// STATIC WHITE
// ============================================================

void setLedOn()
{
    saveLastActiveMode(
        static_cast<uint8_t>(LedMode::STATIC_WHITE)
    );

    if (led.getMode() == LedMode::STATIC_WHITE)
    {
        Serial.println(
            "[LED] STATIC WHITE juz aktywny."
        );

        return;
    }

    Serial.println(
        "[LED] ON - STATIC WHITE"
    );

    led.turnOn();

    if (!saveCurrentState())
    {
        Serial.println(
            "[LED] UWAGA: nie zapisano trybu."
        );
    }
}

// ============================================================
// FLOW
// ============================================================

void setLedFlow()
{
    saveLastActiveMode(
        static_cast<uint8_t>(LedMode::FLOW)
    );

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
// OFF
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

    // Nie nadpisujemy ostatniego aktywnego trybu.
    // LedController sam obsługuje FLOW_OFF.

    led.turnOff();

    if (!saveCurrentState())
    {
        Serial.println(
            "[LED] UWAGA: nie zapisano trybu OFF."
        );
    }
}

// ============================================================
// SERIAL COMMANDS
// ============================================================

void handleSerialCommands()
{
    while (Serial.available() > 0)
    {
        char command = Serial.read();

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
                Serial.println("[SERIAL] Nieznana komenda.");
                Serial.println("1 = STATIC WHITE");
                Serial.println("2 = FLOW");
                Serial.println("0 = OFF");
                break;
        }
    }
}

// ============================================================
// REMOTE COMMANDS
// ============================================================

void handleRemoteCommands()
{
    remote.update();
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

    if (!storage.saveTimestamp(timestamp))
    {
        Serial.println(
            "[STORAGE] BLAD ZAPISU TIMESTAMP."
        );

        return false;
    }

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

    if (!storage.loadTimestamp(savedTimestamp))
    {
        Serial.println("[POWER] BLAD ODCZYTU TIMESTAMP.");
        Serial.println("[POWER] LED -> OFF");

        led.restoreMode(LedMode::OFF);

        saveCurrentState();

        return;
    }

    if (
        savedTimestamp == 0 ||
        savedTimestamp == 0xFFFFFFFF
    )
    {
        Serial.println("[POWER] BRAK POPRAWNEGO TIMESTAMP.");
        Serial.println("[POWER] LED -> OFF");

        led.restoreMode(LedMode::OFF);

        saveCurrentState();

        return;
    }

    bool modeValid =
        storage.loadLedMode(savedMode);

    // --------------------------------------------------------
    // MIGRACJA STAREGO ZAPISU
    // --------------------------------------------------------

    // Jeśli nowy zapis ostatniego aktywnego trybu jeszcze
    // nie istnieje, a stary zapis wskazuje aktywny tryb,
    // wykorzystujemy go do inicjalizacji nowej komórki.

    uint8_t lastActiveMode = 0xFF;

    if (!storage.loadLastActiveLedMode(lastActiveMode))
    {
        if (
            modeValid &&
            (
                savedMode ==
                    static_cast<uint8_t>(LedMode::STATIC_WHITE) ||
                savedMode ==
                    static_cast<uint8_t>(LedMode::FLOW)
            )
        )
        {
            storage.saveLastActiveLedMode(savedMode);
        }
    }

    RtcDateTime now;

    if (!rtc.getDateTime(now))
    {
        Serial.println("[POWER] BLAD ODCZYTU DS3231.");
        Serial.println("[POWER] LED -> OFF");

        led.restoreMode(LedMode::OFF);

        return;
    }

    uint32_t currentTimestamp =
        rtc.toTimestamp(now);

    Serial.println();
    Serial.println("========================================");
    Serial.println("             POWER LOSS CHECK");
    Serial.println("========================================");

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

    if (currentTimestamp < savedTimestamp)
    {
        Serial.println(
            "[POWER] BLAD: RTC jest wczesniejszy "
            "niz zapisany timestamp."
        );

        Serial.println("[POWER] LED -> OFF");

        led.restoreMode(LedMode::OFF);

        saveCurrentState();

        return;
    }

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

        Serial.println("[POWER] LED -> OFF");

        // Ostatni aktywny tryb pozostaje zachowany.

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

        Serial.println("[POWER] LED -> OFF");

        led.restoreMode(LedMode::OFF);

        saveCurrentState();

        return;
    }

    // --------------------------------------------------------
    // RESTORE CURRENT STATE
    // --------------------------------------------------------

    switch (static_cast<LedMode>(savedMode))
    {
        case LedMode::OFF:

            Serial.println("[POWER] Przywracam OFF.");

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

            Serial.println("[POWER] LED -> OFF");

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

    led.begin();

    Wire.begin(
        I2C_SDA,
        I2C_SCL
    );

    Serial.println();
    Serial.println("========================================");
    Serial.println("       LED CONTROLLER - START");
    Serial.println("========================================");

    bool rtcReady = rtc.begin();

    bool storageReady = storage.begin();

    if (rtcReady && storageReady)
    {
        checkPowerLoss();
    }
    else
    {
        Serial.println(
            "[START] RTC lub EEPROM niedostepne."
        );

        Serial.println("[START] LED -> OFF");

        led.restoreMode(LedMode::OFF);
    }

    Serial.printf(
        "[START] Aktualny tryb LED: 0x%02X\n",
        static_cast<uint8_t>(led.getMode())
    );

    // --------------------------------------------------------
    // REMOTE INIT
    // --------------------------------------------------------

    if (!remote.begin())
    {
        Serial.println(
            "[REMOTE] UWAGA: odbiornik pilota niedostepny."
        );
    }

    lastSaveMillis = millis();

    Serial.println();
    Serial.println("Normalna praca uruchomiona.");

    Serial.printf(
        "Heartbeat STORAGE: co %lu sekund.\n",
        SAVE_INTERVAL
    );

    Serial.printf(
        "Limit zaniku zasilania: %lu sekund.\n",
        LONG_OUTAGE
    );

    Serial.println();
    Serial.println("STEROWANIE SERIAL:");
    Serial.println("1 = STATIC WHITE");
    Serial.println("2 = FLOW");
    Serial.println("0 = OFF");

    Serial.println();
    Serial.println("STEROWANIE PILOTEM:");
    Serial.println("W = STATIC WHITE");
    Serial.println("ON = LAST ACTIVE MODE");
    Serial.println("OFF = LED OFF");
    Serial.println("GROUP 1 TOP = FLOW");
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    handleSerialCommands();

    handleRemoteCommands();

    led.update();

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