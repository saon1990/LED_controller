#include <Arduino.h>
#include <Wire.h>

// ============================================================
// ESP32-S3
// ============================================================

#define I2C_SDA 8
#define I2C_SCL 9

// ============================================================
// I2C
// ============================================================

#define RTC_ADDR    0x68
#define EEPROM_ADDR 0x57

// ============================================================
// EEPROM
// ============================================================

#define EEPROM_TIMESTAMP_ADDR 0x0010

// ============================================================
// USTAWIENIA
// ============================================================

// Heartbeat co 60 sekund
#define SAVE_INTERVAL 60UL

// Powyżej 5 minut = długi zanik
#define LONG_OUTAGE 300UL


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

    return days * 86400UL
         + (uint32_t)hour * 3600UL
         + (uint32_t)minute * 60UL
         + second;
}


// ============================================================
// ODCZYT AT24C32
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
        length
    ) != length)
    {
        return false;
    }

    for (uint8_t i = 0; i < length; i++)
    {
        data[i] = Wire.read();
    }

    return true;
}


// ============================================================
// ZAPIS AT24C32
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
    {
        Wire.write(data[i]);
    }

    if (Wire.endTransmission() != 0)
        return false;

    delay(10);

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
        4
    ))
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
// NORMALNY HEARTBEAT
// ============================================================

void saveCurrentTimestamp()
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
        second
    ))
    {
        Serial.println(
            "[EEPROM] BLAD ODCZYTU RTC"
        );

        return;
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

    if (saveTimestamp(timestamp))
    {
        Serial.printf(
            "[EEPROM] Heartbeat: %04d-%02d-%02d %02d:%02d:%02d | %lu\n",
            year,
            month,
            day,
            hour,
            minute,
            second,
            timestamp
        );
    }
    else
    {
        Serial.println(
            "[EEPROM] BLAD ZAPISU"
        );
    }
}


// ============================================================
// SPRAWDZENIE ZANIKU ZASILANIA
// ============================================================

bool checkPowerLoss()
{
    uint32_t savedTimestamp;

    // --------------------------------------------------------
    // Najpierw sprawdzamy EEPROM
    // --------------------------------------------------------

    if (!eepromRead(
        EEPROM_TIMESTAMP_ADDR,
        (uint8_t*)&savedTimestamp,
        0
    ))
    {
        // Ten fragment nie jest używany.
    }

    if (!readSavedTimestamp(savedTimestamp))
    {
        Serial.println(
            "EEPROM: brak mozliwosci odczytu."
        );

        return false;
    }


    // --------------------------------------------------------
    // Sprawdzamy czy EEPROM zawiera dane
    // --------------------------------------------------------

    if (savedTimestamp == 0 ||
        savedTimestamp == 0xFFFFFFFF)
    {
        Serial.println();
        Serial.println(
            "EEPROM: brak zapisanego timestampu."
        );

        Serial.println(
            "Pierwsze uruchomienie - brak analizy zaniku."
        );

        return false;
    }


    // --------------------------------------------------------
    // Odczyt aktualnego RTC
    // --------------------------------------------------------

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
        second
    ))
    {
        Serial.println(
            "BLAD: Nie mozna odczytac DS3231!"
        );

        return false;
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


    // --------------------------------------------------------
    // Informacje
    // --------------------------------------------------------

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
        "Aktualny RTC:       %04d-%02d-%02d %02d:%02d:%02d\n",
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

    Serial.println(currentTimestamp);

    Serial.print(
        "Zapisany timestamp:   "
    );

    Serial.println(savedTimestamp);


    // --------------------------------------------------------
    // Timestamp z przyszłości
    // --------------------------------------------------------

    if (currentTimestamp < savedTimestamp)
    {
        Serial.println();

        Serial.println(
            "BLAD: timestamp EEPROM jest z przyszlosci."
        );

        return false;
    }


    // --------------------------------------------------------
    // Obliczenie różnicy
    // --------------------------------------------------------

    uint32_t difference =
        currentTimestamp - savedTimestamp;

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

    Serial.print(difference);

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


    // --------------------------------------------------------
    // Klasyfikacja
    // --------------------------------------------------------

    if (difference >= LONG_OUTAGE)
    {
        Serial.println();

        Serial.println(
            "WYNIK: DLUGI ZANIK ZASILANIA"
        );

        Serial.println(
            "LED powinny pozostac WYLACZONE."
        );
    }
    else
    {
        Serial.println();

        Serial.println(
            "WYNIK: KROTKI ZANIK ZASILANIA"
        );

        Serial.println(
            "Mozemy przywrocic poprzedni stan LED."
        );
    }

    Serial.println();

    return true;
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);

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


    // ========================================================
    // NAJPIERW CHECK
    // ========================================================

    bool previousStateExists =
        checkPowerLoss();


    // ========================================================
    // NORMALNE DZIAŁANIE
    // ========================================================

    Serial.println();

    if (!previousStateExists)
    {
        Serial.println(
            "Brak poprzedniego timestampu."
        );

        Serial.println(
            "Normalne uruchomienie."
        );
    }
    else
    {
        Serial.println(
            "Analiza poprzedniego stanu zakonczona."
        );
    }


    // ========================================================
    // WAŻNE:
    //
    // NIE zapisujemy timestampu od razu po starcie.
    //
    // Pierwszy heartbeat dopiero po 60 sekundach.
    // Dzięki temu po powrocie zasilania mamy czas na
    // sprawdzenie starego timestampu.
    // ========================================================

    lastSaveMillis = millis();


    Serial.println();

    Serial.println(
        "Normalna praca uruchomiona."
    );

    Serial.println(
        "Heartbeat EEPROM: co 60 sekund."
    );

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

    if (millis() - lastSaveMillis >=
        SAVE_INTERVAL * 1000UL)
    {
        lastSaveMillis = millis();

        saveCurrentTimestamp();
    }

    // ========================================================
    // Tutaj później dojdzie sterowanie LED
    // ========================================================

    delay(100);
}