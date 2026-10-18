#include <Arduino.h>
#include <SPI.h>

// ESP32-S3 DevKitC-1 N8R8 -> CC2500
constexpr int PIN_SCK  = 4;
constexpr int PIN_MOSI = 5;
constexpr int PIN_MISO = 6;
constexpr int PIN_CSN  = 7;
constexpr int PIN_GDO0 = 15;

constexpr uint32_t SPI_CLOCK = 1000000;
constexpr float XOSC_HZ = 26000000.0f;

SPISettings spiSettings(SPI_CLOCK, MSBFIRST, SPI_MODE0);
const float frequenciesMHz[] = {2410.000f, 2441.000f, 2472.500f};
constexpr int FREQUENCY_COUNT = 3;
int currentFrequency = 0;

// CC2500 strobes
constexpr uint8_t SRES  = 0x30;
constexpr uint8_t SCAL  = 0x33;
constexpr uint8_t SRX   = 0x34;
constexpr uint8_t SIDLE = 0x36;
constexpr uint8_t SFRX  = 0x3A;

// CC2500 configuration registers
constexpr uint8_t IOCFG0   = 0x02;
constexpr uint8_t FIFOTHR  = 0x03;
constexpr uint8_t PKTCTRL0 = 0x08;
constexpr uint8_t FSCTRL1  = 0x0B;
constexpr uint8_t FSCTRL0  = 0x0C;
constexpr uint8_t FREQ2    = 0x0D;
constexpr uint8_t FREQ1    = 0x0E;
constexpr uint8_t FREQ0    = 0x0F;
constexpr uint8_t MDMCFG4  = 0x10;
constexpr uint8_t MDMCFG3  = 0x11;
constexpr uint8_t MDMCFG2  = 0x12;
constexpr uint8_t MDMCFG1  = 0x13;
constexpr uint8_t MDMCFG0  = 0x14;
constexpr uint8_t MCSM0    = 0x18;
constexpr uint8_t AGCCTRL2 = 0x1B;
constexpr uint8_t AGCCTRL1 = 0x1C;
constexpr uint8_t AGCCTRL0 = 0x1D;

// CC2500 status registers
constexpr uint8_t PARTNUM   = 0x30;
constexpr uint8_t VERSION   = 0x31;
constexpr uint8_t RSSI_REG  = 0x34;
constexpr uint8_t MARCSTATE = 0x35;

uint8_t readConfig(uint8_t address) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_CSN, LOW);
  delayMicroseconds(2);
  SPI.transfer(address | 0x80);
  uint8_t value = SPI.transfer(0x00);
  digitalWrite(PIN_CSN, HIGH);
  SPI.endTransaction();
  return value;
}

uint8_t readStatus(uint8_t address) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_CSN, LOW);
  delayMicroseconds(2);
  SPI.transfer(address | 0xC0);
  uint8_t value = SPI.transfer(0x00);
  digitalWrite(PIN_CSN, HIGH);
  SPI.endTransaction();
  return value;
}

void writeRegister(uint8_t address, uint8_t value) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_CSN, LOW);
  delayMicroseconds(2);
  SPI.transfer(address);
  SPI.transfer(value);
  digitalWrite(PIN_CSN, HIGH);
  SPI.endTransaction();
}

uint8_t strobe(uint8_t command) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_CSN, LOW);
  delayMicroseconds(2);
  uint8_t status = SPI.transfer(command);
  digitalWrite(PIN_CSN, HIGH);
  SPI.endTransaction();
  return status;
}

void resetCC2500() {
  strobe(SRES);
  delay(5);
}

void configureReceiver() {
  // GDO0 outputs Carrier Sense. This test measures RSSI and MARCSTATE.
  writeRegister(IOCFG0, 0x0E);
  writeRegister(FIFOTHR, 0x07);

  // Asynchronous serial mode avoids filling the RX FIFO with arbitrary
  // demodulated noise while we only measure RSSI.
  writeRegister(PKTCTRL0, 0x32);

  writeRegister(FSCTRL1, 0x0C);
  writeRegister(FSCTRL0, 0x00);

  // Wide diagnostic receive bandwidth; not a confirmed pilot configuration.
  writeRegister(MDMCFG4, 0x03);
  writeRegister(MDMCFG3, 0x93);
  writeRegister(MDMCFG2, 0x00);
  writeRegister(MDMCFG1, 0x22);
  writeRegister(MDMCFG0, 0xF8);

  writeRegister(MCSM0, 0x18);
  writeRegister(AGCCTRL2, 0xC7);
  writeRegister(AGCCTRL1, 0x18);
  writeRegister(AGCCTRL0, 0xB0);
}

void setFrequency(float freqMHz) {
  uint32_t word = (uint32_t)((freqMHz * 1000000.0f * 65536.0f) / XOSC_HZ);

  strobe(SIDLE);
  writeRegister(FREQ2, (word >> 16) & 0xFF);
  writeRegister(FREQ1, (word >> 8) & 0xFF);
  writeRegister(FREQ0, word & 0xFF);

  strobe(SCAL);
  delay(3);
  strobe(SRX);
  delay(10);
}

float rssiToDbm(uint8_t raw) {
  return ((float)(int8_t)raw / 2.0f) - 74.0f;
}

void printMenu() {
  Serial.println();
  Serial.println("=========== MENU ===========");
  Serial.println("1 - ustaw 2410.000 MHz");
  Serial.println("2 - ustaw 2441.000 MHz");
  Serial.println("3 - ustaw 2472.500 MHz");
  Serial.println("i - pomiar IDLE (bez pilota)");
  Serial.println("o - pomiar ON (naciskaj ON podczas pomiaru)");
  Serial.println("f - pomiar OFF (naciskaj OFF podczas pomiaru)");
  Serial.println("h - wyswietl menu");
  Serial.println("============================");
}

void measure(char phase) {
  const char* phaseName = (phase == 'i') ? "IDLE" : (phase == 'o') ? "ON" : "OFF";
  Serial.println();
  Serial.printf("FAZA=%s | FREQ=%.3f MHz\n", phaseName, frequenciesMHz[currentFrequency]);
  Serial.println("Pomiar rozpocznie sie za 3 sekundy.");

  for (int i = 3; i >= 1; --i) {
    Serial.printf("%d...\n", i);
    delay(1000);
  }

  strobe(SRX);
  delay(20);

  int minRaw = 255;
  int maxRaw = 0;
  uint32_t sumRaw = 0;
  uint32_t samples = 0;
  uint32_t gdoHigh = 0;
  uint32_t rxCount = 0;
  uint32_t overflowCount = 0;
  uint32_t otherStateCount = 0;

  uint32_t start = millis();
  uint32_t nextPrint = start + 100;
  Serial.println("czas_ms RSSI_raw RSSI_dBm MARCSTATE GDO0");

  while (millis() - start < 5000) {
    uint8_t raw = readStatus(RSSI_REG);
    uint8_t state = readStatus(MARCSTATE) & 0x1F;
    int gdo = digitalRead(PIN_GDO0);

    if (raw < minRaw) minRaw = raw;
    if (raw > maxRaw) maxRaw = raw;
    sumRaw += raw;
    samples++;

    if (gdo == HIGH) gdoHigh++;
    if (state == 0x0D) rxCount++;
    else if (state == 0x11) overflowCount++;
    else otherStateCount++;

    uint32_t elapsed = millis() - start;
    if ((int32_t)(millis() - nextPrint) >= 0) {
      Serial.printf("%lu %u %.1f 0x%02X %d\n",
                    (unsigned long)elapsed, raw, rssiToDbm(raw), state, gdo);
      nextPrint += 100;
    }
    delay(10);
  }

  Serial.println("--- PODSUMOWANIE ---");
  Serial.printf("Liczba probek: %lu\n", (unsigned long)samples);
  if (samples > 0) {
    Serial.printf("RSSI raw min/max/srednia: %d / %d / %.2f\n",
                  minRaw, maxRaw, (float)sumRaw / samples);
    Serial.printf("RSSI dBm min/max: %.1f / %.1f\n",
                  rssiToDbm((uint8_t)minRaw), rssiToDbm((uint8_t)maxRaw));
    Serial.printf("GDO0 HIGH: %lu/%lu\n",
                  (unsigned long)gdoHigh, (unsigned long)samples);
    Serial.printf("MARCSTATE RX(0x0D): %lu\n", (unsigned long)rxCount);
    Serial.printf("MARCSTATE RXFIFO_OVERFLOW(0x11): %lu\n",
                  (unsigned long)overflowCount);
    Serial.printf("MARCSTATE inne: %lu\n", (unsigned long)otherStateCount);
  }

  if (overflowCount > 0) {
    Serial.println("UWAGA: nadal wykryto RXFIFO_OVERFLOW.");
  } else if (rxCount == samples) {
    Serial.println("OK: odbiornik pozostal w RX przez caly pomiar.");
  } else {
    Serial.println("UWAGA: odbiornik opuszczal stan RX; sprawdz stany MARCSTATE.");
  }
  Serial.println("Pomiar zakonczony.");
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(PIN_CSN, OUTPUT);
  digitalWrite(PIN_CSN, HIGH);
  pinMode(PIN_GDO0, INPUT);

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);

  Serial.println();
  Serial.println("ESP32-S3 + CC2500 - RSSI / MARCSTATE TEST");

  resetCC2500();
  Serial.printf("PARTNUM=0x%02X\n", readStatus(PARTNUM));
  Serial.printf("VERSION=0x%02X\n", readStatus(VERSION));

  configureReceiver();
  setFrequency(frequenciesMHz[currentFrequency]);

  Serial.printf("Startowa czestotliwosc: %.3f MHz\n", frequenciesMHz[currentFrequency]);
  Serial.printf("MARCSTATE po konfiguracji: 0x%02X\n", readStatus(MARCSTATE) & 0x1F);
  printMenu();
}

void loop() {
  if (!Serial.available()) {
    delay(10);
    return;
  }

  char cmd = Serial.read();
  if (cmd == '\r' || cmd == '\n' || cmd == ' ') return;

  if (cmd >= '1' && cmd <= '3') {
    currentFrequency = cmd - '1';
    setFrequency(frequenciesMHz[currentFrequency]);
    Serial.printf("Ustawiono %.3f MHz\n", frequenciesMHz[currentFrequency]);
    Serial.printf("MARCSTATE=0x%02X\n", readStatus(MARCSTATE) & 0x1F);
  } else if (cmd == 'i' || cmd == 'I' || cmd == 'o' || cmd == 'O' ||
             cmd == 'f' || cmd == 'F') {
    char phase = cmd;
    if (phase >= 'A' && phase <= 'Z') phase += ('a' - 'A');
    measure(phase);
  } else if (cmd == 'h' || cmd == 'H') {
    printMenu();
  } else {
    Serial.println("Nieznana komenda. Wpisz h, aby wyswietlic menu.");
  }
}
