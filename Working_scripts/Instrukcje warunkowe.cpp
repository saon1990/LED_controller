#include <Arduino.h>
#include <Wire.h>


// ============================================================
// ESP32-S3
// ============================================================

#define I2C_SDA 8
#define I2C_SCL 9

// GPIO używany docelowo jako DATA dla WS2814
// Na obecnym etapie testujemy nim wejście 1A układu AHCT125.
#define DATA_GPIO 1


// ============================================================
// I2C
// ============================================================

#define RTC_ADDR    0x68
#define EEPROM_ADDR 0x57


// ============================================================
// AT24C32 - ADRESY
// ============================================================

// Timestamp:
// 0x0010 - 0x0013

#define EEPROM_TIMESTAMP_ADDR 0x0010


// Stan LED:
// 0x0014
//
// 0x00 = OFF
// 0x01 = ON

#define EEPROM_LED_STATE_ADDR 0x0014


// Marker poprawności danych LED
#define EEPROM_LED_VALID_ADDR 0x0015

#define LED_VALID_MARKER 0xA5


// ============================================================
// USTAWIENIA
// ============================================================

// Heartbeat co 60 sekund
#define SAVE_INTERVAL 60UL


// 5 minut
//
// >= 300 sekund = długi zanik
// < 300 sekund  = krótki zanik

#define LONG_OUTAGE 300UL


// ============================================================
// SYMULOWANY STAN LED
// ============================================================
//
// Na tym etapie nie mamy jeszcze podłączonej taśmy.
//
// true  = LED ON
// false = LED OFF
//
// Później ta zmienna będzie reprezentowała rzeczywisty
// stan sterownika LED.
//

bool ledState = false;


// ============================================================
// ZMIENNE
// ============================================================

unsigned long lastSaveMillis = 0;


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

    if (Wire.requestFrom(
        (uint8_t)RTC_ADDR,
        (uint8_t)7
    ) != 7)
    {
        return false;
    }

    uint8_t sec = Wire.read();
    uint8_t min = Wire.read();
    uint8_t hrs = Wire.read();

    // Dzień tygodnia
    Wire.read();

    uint8_t date = Wire.read();
    uint8_t mon  = Wire.read();
    uint8_t yr   = Wire.read();

    second = bcdToDec(sec & 0x7F);
    minute = bcdToDec(min & 0x7F);
    hour   = bcdToDec(hrs & 0x3F);

    day   = bcdToDec(date & 0x3F);
    month = bcdToDec(mon & 0x1F);

    year = 2000 + bcdToDec(yr);

    return true;
}


// ============================================================
// DNI W MIESIĄCU
// ============================================================

uint8_t daysInMonth(
    uint16_t year,
    uint8_t month
)
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
//
// Sekundy od:
// 2000-01-01 00:00:00
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

    for (
        uint16_t y = 2000;
        y < year;
        y++
    )
    {
        bool leap =
            ((y % 4 == 0) &&
             ((y % 100 != 0) ||
              (y % 400 == 0)));

        days += leap ? 366 : 365;
    }

    for (
        uint8_t m = 1;
        m < month;
        m++
    )
    {
        days += daysInMonth(
            year,
            m
        );
    }

    days += day - 1;

    return
        days * 86400UL
        + (uint32_t)hour * 3600UL
        + (uint32_t)minute * 60UL
        + second;
}


// ============================================================
// AT24C32 - WRITE
// ============================================================

bool eepromWrite(
    uint16_t address,
    const uint8_t* data,
    uint8_t length
)
{
    Wire.beginTransmission(
        EEPROM_ADDR
    );

    Wire.write(
        (address >> 8) & 0xFF
    );

    Wire.write(
        address & 0xFF
    );

    for (
        uint8_t i = 0;
        i < length;
        i++
    )
    {
        Wire.write(data[i]);
    }

    if (
        Wire.endTransmission() != 0
    )
    {
        return false;
    }

    // Czas zapisu EEPROM
    delay(10);

    return true;
}


// ============================================================
// AT24C32 - READ
// ============================================================

bool eepromRead(
    uint16_t address,
    uint8_t* data,
    uint8_t length
)
{
    Wire.beginTransmission(
        EEPROM_ADDR
    );

    Wire.write(
        (address >> 8) & 0xFF
    );

    Wire.write(
        address & 0xFF
    );

    if (
        Wire.endTransmission(false) != 0
    )
    {
        return false;
    }

    if (
        Wire.requestFrom(
            (uint8_t)EEPROM_ADDR,
            length
        ) != length
    )
    {
        return false;
    }

    for (
        uint8_t i = 0;
        i < length;
        i++
    )
    {
        data[i] = Wire.read();
    }

    return true;
}


// ============================================================
// ODCZYT TIMESTAMPU
// ============================================================

bool readSavedTimestamp(
    uint32_t& timestamp
)
{
    uint8_t buffer[4];

    if (
        !eepromRead(
            EEPROM_TIMESTAMP_ADDR,
            buffer,
            4
        )
    )
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

bool readSavedLedState(
    bool& state
)
{
    uint8_t valid;
    uint8_t value;

    // --------------------------------------------------------
    // Najpierw sprawdzamy marker
    // --------------------------------------------------------

    if (
        !eepromRead(
            EEPROM_LED_VALID_ADDR,
            &valid,
            1
        )
    )
    {
        return false;
    }

    if (
        valid != LED_VALID_MARKER
    )
    {
        return false;
    }


    // --------------------------------------------------------
    // Odczyt stanu LED
    // --------------------------------------------------------

    if (
        !eepromRead(
            EEPROM_LED_STATE_ADDR,
            &value,
            1
        )
    )
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

bool saveLedState(
    bool state
)
{
    uint8_t value =
        state ? 0x01 : 0x00;


    // --------------------------------------------------------
    // Najpierw stan
    // --------------------------------------------------------

    if (
        !eepromWrite(
            EEPROM_LED_STATE_ADDR,
            &value,
            1
        )
    )
    {
        return false;
    }


    // --------------------------------------------------------
    // Następnie marker
    // --------------------------------------------------------

    uint8_t marker =
        LED_VALID_MARKER;

    if (
        !eepromWrite(
            EEPROM_LED_VALID_ADDR,
            &marker,
            1
        )
    )
    {
        return false;
    }


    return true;
}


// ============================================================
// ZAPIS TIMESTAMPU
// ============================================================

bool saveTimestamp(
    uint32_t timestamp
)
{
    uint8_t buffer[4];

    buffer[0] =
        (timestamp >> 24) & 0xFF;

    buffer[1] =
        (timestamp >> 16) & 0xFF;

    buffer[2] =
        (timestamp >> 8) & 0xFF;

    buffer[3] =
        timestamp & 0xFF;


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


    // --------------------------------------------------------
    // RTC
    // --------------------------------------------------------

    if (
        !rtcRead(
            year,
            month,
            day,
            hour,
            minute,
            second
        )
    )
    {
        Serial.println(
            "[EEPROM] BLAD ODCZYTU RTC"
        );

        return false;
    }


    // --------------------------------------------------------
    // Timestamp
    // --------------------------------------------------------

    uint32_t timestamp =
        dateTimeToTimestamp(
            year,
            month,
            day,
            hour,
            minute,
            second
        );


    // --------------------------------------------------------
    // Zapis timestampu
    // --------------------------------------------------------

    if (
        !saveTimestamp(
            timestamp
        )
    )
    {
        Serial.println(
            "[EEPROM] BLAD ZAPISU TIMESTAMPU"
        );

        return false;
    }


    // --------------------------------------------------------
    // Zapis stanu LED
    // --------------------------------------------------------

    if (
        !saveLedState(
            ledState
        )
    )
    {
        Serial.println(
            "[EEPROM] BLAD ZAPISU STANU LED"
        );

        return false;
    }


    // --------------------------------------------------------
    // Informacja
    // --------------------------------------------------------

    Serial.printf(
        "[EEPROM] Heartbeat: "
        "%04d-%02d-%02d %02d:%02d:%02d "
        "| timestamp=%lu "
        "| LED=%s\n",

        year,
        month,
        day,

        hour,
        minute,
        second,

        timestamp,

        ledState
            ? "ON"
            : "OFF"
    );


    return true;
}


// ============================================================
// SPRAWDZENIE ZANIKU ZASILANIA
// ============================================================

void checkPowerLoss()
{
    uint32_t savedTimestamp;


    // ========================================================
    // 1. ODCZYT TIMESTAMPU
    // ========================================================

    if (
        !readSavedTimestamp(
            savedTimestamp
        )
    )
    {
        Serial.println();
        Serial.println(
            "EEPROM: nie mozna odczytac timestampu."
        );

        Serial.println(
            "Normalne uruchomienie."
        );

        return;
    }


    // ========================================================
    // 2. BRAK POPRAWNEGO TIMESTAMPU
    // ========================================================

    if (
        savedTimestamp == 0 ||
        savedTimestamp == 0xFFFFFFFF
    )
    {
        Serial.println();
        Serial.println(
            "EEPROM: brak zapisanego timestampu."
        );

        Serial.println(
            "Pierwsze uruchomienie."
        );

        Serial.println(
            "Normalne uruchomienie."
        );

        return;
    }


    // ========================================================
    // 3. ODCZYT POPRZEDNIEGO STANU LED
    // ========================================================

    bool savedLedState;

    bool ledStateValid =
        readSavedLedState(
            savedLedState
        );


    // ========================================================
    // 4. ODCZYT RTC
    // ========================================================

    uint16_t year;

    uint8_t month;
    uint8_t day;

    uint8_t hour;
    uint8_t minute;
    uint8_t second;


    if (
        !rtcRead(
            year,
            month,
            day,
            hour,
            minute,
            second
        )
    )
    {
        Serial.println();
        Serial.println(
            "BLAD: Nie mozna odczytac DS3231!"
        );

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


    // ========================================================
    // 5. NAGŁÓWEK
    // ========================================================

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


    Serial.print(
        "Aktualny timestamp:  "
    );

    Serial.println(
        currentTimestamp
    );


    Serial.print(
        "Zapisany timestamp:   "
    );

    Serial.println(
        savedTimestamp
    );


    // ========================================================
    // 6. ZAPISANY STAN LED
    // ========================================================

    if (ledStateValid)
    {
        Serial.print(
            "Zapisany stan LED:   "
        );

        Serial.println(
            savedLedState
                ? "ON"
                : "OFF"
        );
    }
    else
    {
        Serial.println(
            "Zapisany stan LED:   BRAK"
        );
    }


    // ========================================================
    // 7. SPRAWDZENIE RTC
    // ========================================================

    if (
        currentTimestamp < savedTimestamp
    )
    {
        Serial.println();

        Serial.println(
            "BLAD: timestamp EEPROM "
            "jest z przyszlosci."
        );

        return;
    }


    // ========================================================
    // 8. OBLICZENIE CZASU
    // ========================================================

    uint32_t difference =
        currentTimestamp -
        savedTimestamp;


    uint32_t hours =
        difference / 3600;


    uint32_t minutes =
        (difference % 3600) / 60;


    uint32_t seconds =
        difference % 60;


    Serial.println();

    Serial.println(
        "----------------------------------------"
    );


    Serial.print(
        "Czas od ostatniego heartbeat: "
    );

    Serial.print(
        difference
    );

    Serial.println(
        " sekund"
    );


    Serial.printf(
        "Czyli: %lu h %lu min %lu s\n",

        hours,
        minutes,
        seconds
    );


    Serial.println(
        "----------------------------------------"
    );


    // ========================================================
    // 9. DECYZJA O STANIE LED
    // ========================================================

    if (
        difference >= LONG_OUTAGE
    )
    {
        // ----------------------------------------------------
        // DŁUGI ZANIK
        // ----------------------------------------------------

        ledState = false;


        Serial.println();

        Serial.println(
            "WYNIK: DLUGI ZANIK ZASILANIA"
        );

        Serial.println(
            "LED -> OFF"
        );
    }
    else
    {
        // ----------------------------------------------------
        // KRÓTKI ZANIK
        // ----------------------------------------------------

        Serial.println();

        Serial.println(
            "WYNIK: KROTKI ZANIK ZASILANIA"
        );


        if (!ledStateValid)
        {
            // ------------------------------------------------
            // Brak zapisanego stanu.
            //
            // Bezpieczna opcja:
            // OFF
            // ------------------------------------------------

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
            // ------------------------------------------------
            // Przywracamy poprzedni stan
            // ------------------------------------------------

            ledState =
                savedLedState;


            Serial.print(
                "Poprzedni stan LED: "
            );

            Serial.println(
                savedLedState
                    ? "ON"
                    : "OFF"
            );


            Serial.print(
                "LED -> "
            );

            Serial.println(
                ledState
                    ? "ON"
                    : "OFF"
            );
        }
    }


    Serial.println();
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);


    // ========================================================
    // TEST AHCT125
    // ========================================================
    //
    // GPIO1 jest podłączony do:
    //
    // ESP32 GPIO1 -> AHCT125 pin 2 (1A)
    //
    // AHCT125 pin 3 (1Y) jest obecnie jeszcze niepodłączony.
    //
    // Na tym etapie nie mamy jeszcze podłączonej taśmy LED
    // ani rezystora 330 Ω.
    //
    // Będziemy przełączać GPIO1:
    //
    // HIGH -> AHCT125 pin 3 powinien mieć około 5 V
    // LOW  -> AHCT125 pin 3 powinien mieć około 0 V
    //
    // ========================================================

    pinMode(
        DATA_GPIO,
        OUTPUT
    );

    digitalWrite(
        DATA_GPIO,
        LOW
    );


    Serial.println(
        "TEST AHCT125 - GPIO1"
    );


    // ========================================================
    // I2C
    // ========================================================

    Wire.begin(
        I2C_SDA,
        I2C_SCL
    );


    // ========================================================
    // START
    // ========================================================

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


    // ========================================================
    // NAJPIERW CHECK
    //
    // Bardzo ważne:
    //
    // NICZEGO JESZCZE NIE ZAPISUJEMY.
    //
    // Najpierw odczytujemy poprzedni timestamp
    // i poprzedni stan LED.
    // ========================================================

    checkPowerLoss();


    // ========================================================
    // NORMALNA PRACA
    // ========================================================

    Serial.println();

    Serial.println(
        "Normalna praca uruchomiona."
    );

    Serial.println(
        "Heartbeat EEPROM: co 60 sekund."
    );


    Serial.print(
        "Aktualny stan LED: "
    );

    Serial.println(
        ledState
            ? "ON"
            : "OFF"
    );


    // ========================================================
    // PIERWSZY HEARTBEAT DOPIERO PO 60 SEKUNDACH
    // ========================================================

    lastSaveMillis =
        millis();


    Serial.println();
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
    // ========================================================
    // HEARTBEAT CO 60 SEKUND
    // ========================================================

    if (
        millis() - lastSaveMillis
        >= SAVE_INTERVAL * 1000UL
    )
    {
        lastSaveMillis =
            millis();

        saveCurrentState();
    }


    // ========================================================
    // TEST AHCT125
    //
    // GPIO1 -> pin 2 (1A)
    // pin 3 (1Y) -> pomiar multimetrem
    //
    // HIGH = około 5 V na pinie 3
    // LOW  = około 0 V na pinie 3
    // ========================================================

    digitalWrite(
        DATA_GPIO,
        HIGH
    );

    Serial.println(
        "GPIO1 = HIGH"
    );

    delay(10000);


    digitalWrite(
        DATA_GPIO,
        LOW
    );

    Serial.println(
        "GPIO1 = LOW"
    );

    delay(10000);
}