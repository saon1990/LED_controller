#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <math.h>

// ESP32-S3 DevKitC-1 N8R8 <-> CC2500
constexpr int PIN_SCK  = 4;
constexpr int PIN_MOSI = 5;
constexpr int PIN_MISO = 6;
constexpr int PIN_CSN  = 7;
constexpr int PIN_GDO0 = 15;

constexpr uint32_t SPI_CLOCK = 1000000;
constexpr double XOSC_HZ = 26000000.0;

// Scan: 2400.000-2483.500 MHz, 250 kHz step, approx. 100 ms/frequency
constexpr float START_MHZ = 2400.000f;
constexpr float STOP_MHZ  = 2483.500f;
constexpr float STEP_MHZ  = 0.250f;
constexpr uint32_t RSSI_SAMPLE_INTERVAL_MS = 10;
constexpr uint8_t RSSI_SAMPLES = 10;
constexpr uint32_t SETTLE_AFTER_RX_MS = 5;
constexpr size_t SCAN_POINTS = 335;

SPISettings spiSettings(SPI_CLOCK, MSBFIRST, SPI_MODE0);

// CC2500 command strobes
constexpr uint8_t SRES  = 0x30;
constexpr uint8_t SCAL  = 0x33;
constexpr uint8_t SRX   = 0x34;
constexpr uint8_t SIDLE = 0x36;

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

// GDO0_CFG=0x0D: raw asynchronous demodulator data.
// PKT_FORMAT=3 in PKTCTRL0 selects asynchronous serial mode.
constexpr uint8_t GDO0_ASYNC_DATA = 0x0D;

struct ScanResult {
  float frequencyMHz;
  float rssiAvgDbm;
  float rssiMinDbm;
  float rssiMaxDbm;
  uint32_t edges;
};

ScanResult idleResults[SCAN_POINTS];
ScanResult onResults[SCAN_POINTS];
volatile bool countEdges = false;
volatile uint32_t edgeCount = 0;

void IRAM_ATTR gdo0EdgeISR() {
  if (countEdges) {
    edgeCount++;
  }
}

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
  writeRegister(FIFOTHR, 0x07);
  writeRegister(PKTCTRL0, 0x32);  // asynchronous serial mode
  writeRegister(FSCTRL1, 0x0C);
  writeRegister(FSCTRL0, 0x00);

  // Broad diagnostic settings; not yet confirmed to match the remote.
  writeRegister(MDMCFG4, 0x03);
  writeRegister(MDMCFG3, 0x93);
  writeRegister(MDMCFG2, 0x00);
  writeRegister(MDMCFG1, 0x22);
  writeRegister(MDMCFG0, 0xF8);

  writeRegister(MCSM0, 0x18);
  writeRegister(AGCCTRL2, 0xC7);
  writeRegister(AGCCTRL1, 0x18);
  writeRegister(AGCCTRL0, 0xB0);
  writeRegister(IOCFG0, GDO0_ASYNC_DATA);
}

void setFrequency(float freqMHz) {
  const uint32_t word = (uint32_t)((((double)freqMHz * 1000000.0) * 65536.0) / XOSC_HZ);
  strobe(SIDLE);
  writeRegister(FREQ2, (word >> 16) & 0xFF);
  writeRegister(FREQ1, (word >> 8) & 0xFF);
  writeRegister(FREQ0, word & 0xFF);
  strobe(SCAL);
  delay(3);
  strobe(SRX);
  delay(SETTLE_AFTER_RX_MS);
}

float readRssiDbm() {
  const int8_t raw = (int8_t)readStatus(RSSI_REG);
  return ((float)raw / 2.0f) - 74.0f;
}

void printChipInfo() {
  Serial.printf("CC2500 PARTNUM=0x%02X VERSION=0x%02X MARCSTATE=0x%02X\n",
                readStatus(PARTNUM), readStatus(VERSION), readStatus(MARCSTATE) & 0x1F);
}

void printMenu() {
  Serial.println();
  Serial.println(F("======= CC2500: porownanie IDLE vs ON ======="));
  Serial.println(F("s - rozpocznij caly test: IDLE, potem ON"));
  Serial.println(F("r - odczytaj identyfikatory i stan CC2500"));
  Serial.println(F("h - pomoc"));
  Serial.println(F("Zakres 2400.000-2483.500 MHz, krok 250 kHz."));
  Serial.println(F("Kazdy przebieg: ok. 40-45 sekund."));
  Serial.println(F("GDO0 = surowe dane demodulatora; zbocza nie sa dekodowanymi ramkami."));
  Serial.println(F("============================================="));
}

void scanPass(ScanResult *destination, const char *passName, bool tellUserIdle) {
  Serial.println();
  if (tellUserIdle) {
    Serial.println(F("Przebieg IDLE: pilot ma pozostac nietkniety."));
  } else {
    Serial.println(F("Przebieg ON: trzymaj przycisk ON przez CALY skan."));
  }
  Serial.printf("PASS_START,%s\n", passName);
  delay(1000);

  for (size_t i = 0; i < SCAN_POINTS; ++i) {
    const float freq = START_MHZ + ((float)i * STEP_MHZ);
    setFrequency(freq);

    int32_t rssiSumHalfDb = 0;
    float rssiMin = 1000.0f;
    float rssiMax = -1000.0f;

    noInterrupts();
    edgeCount = 0;
    countEdges = true;
    interrupts();

    for (uint8_t sample = 0; sample < RSSI_SAMPLES; ++sample) {
      const float rssi = readRssiDbm();
      const int16_t rssiHalfDb = (int16_t)lroundf(rssi * 2.0f);
      rssiSumHalfDb += rssiHalfDb;
      if (rssi < rssiMin) rssiMin = rssi;
      if (rssi > rssiMax) rssiMax = rssi;
      delay(RSSI_SAMPLE_INTERVAL_MS);
    }

    noInterrupts();
    countEdges = false;
    const uint32_t edges = edgeCount;
    interrupts();

    destination[i].frequencyMHz = freq;
    destination[i].rssiAvgDbm = ((float)rssiSumHalfDb / (float)RSSI_SAMPLES) / 2.0f;
    destination[i].rssiMinDbm = rssiMin;
    destination[i].rssiMaxDbm = rssiMax;
    destination[i].edges = edges;

    // Occasional progress marker, so the user knows the scan is alive.
    if ((i + 1) % 50 == 0 || i + 1 == SCAN_POINTS) {
      Serial.printf("PROGRESS,%s,%u/%u,%.3f MHz\n",
                    passName, (unsigned)(i + 1), (unsigned)SCAN_POINTS, freq);
    }
  }

  strobe(SIDLE);
  Serial.printf("PASS_DONE,%s,MARCSTATE=0x%02X\n",
                passName, readStatus(MARCSTATE) & 0x1F);
}

void printCombinedResults() {
  Serial.println();
  Serial.println(F("COMPARE_DONE"));
  Serial.println(F("freq_mhz,idle_rssi_avg_dbm,on_rssi_avg_dbm,delta_rssi_db,idle_min_dbm,idle_max_dbm,on_min_dbm,on_max_dbm,idle_edges,on_edges,delta_edges"));
  for (size_t i = 0; i < SCAN_POINTS; ++i) {
    const float deltaRssi = onResults[i].rssiAvgDbm - idleResults[i].rssiAvgDbm;
    const int32_t deltaEdges = (int32_t)onResults[i].edges - (int32_t)idleResults[i].edges;
    Serial.printf("%.3f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%lu,%lu,%ld\n",
                  idleResults[i].frequencyMHz,
                  idleResults[i].rssiAvgDbm,
                  onResults[i].rssiAvgDbm,
                  deltaRssi,
                  idleResults[i].rssiMinDbm,
                  idleResults[i].rssiMaxDbm,
                  onResults[i].rssiMinDbm,
                  onResults[i].rssiMaxDbm,
                  (unsigned long)idleResults[i].edges,
                  (unsigned long)onResults[i].edges,
                  (long)deltaEdges);
  }

  // Rank frequencies by largest RSSI increase during ON vs IDLE.
  bool printed[SCAN_POINTS] = {false};
  Serial.println();
  Serial.println(F("TOP 20: najwiekszy wzrost sredniego RSSI (ON minus IDLE)"));
  for (uint8_t rank = 0; rank < 20; ++rank) {
    size_t best = SCAN_POINTS;
    float bestDelta = -10000.0f;
    for (size_t i = 0; i < SCAN_POINTS; ++i) {
      if (!printed[i]) {
        const float delta = onResults[i].rssiAvgDbm - idleResults[i].rssiAvgDbm;
        if (delta > bestDelta) {
          bestDelta = delta;
          best = i;
        }
      }
    }
    if (best == SCAN_POINTS) break;
    printed[best] = true;
    Serial.printf("%2u) %.3f MHz | delta RSSI %+0.1f dB | idle %.1f dBm | ON %.1f dBm | edges idle=%lu ON=%lu delta=%ld\n",
                  (unsigned)(rank + 1),
                  idleResults[best].frequencyMHz,
                  bestDelta,
                  idleResults[best].rssiAvgDbm,
                  onResults[best].rssiAvgDbm,
                  (unsigned long)idleResults[best].edges,
                  (unsigned long)onResults[best].edges,
                  (long)((int32_t)onResults[best].edges - (int32_t)idleResults[best].edges));
  }

  Serial.printf("Stan MARCSTATE po porownaniu: 0x%02X\n", readStatus(MARCSTATE) & 0x1F);
  Serial.println(F("Skopiuj caly log od PASS_START, najlepiej do konca TOP 20."));
}

void runFullComparison() {
  Serial.println();
  Serial.println(F("TEST DWUPRZEBIEGOWY: najpierw IDLE, potem ciagle ON."));
  Serial.println(F("Nie zmieniaj polozenia anteny ani okablowania pomiedzy przebiegami."));
  Serial.println(F("Uwaga: podczas przebiegu ON pilot musi nadawac przez caly skan."));
  delay(1500);

  scanPass(idleResults, "IDLE", true);

  Serial.println();
  Serial.println(F("IDLE zakonczony."));
  Serial.println(F("Przygotuj pilot. Za chwile pojawi sie odliczanie przed skanem ON."));
  Serial.println(F("Gdy zobaczysz GO_ON_NOW, natychmiast wcisnij i trzymaj ON az do ON_SCAN_DONE."));
  for (int i = 5; i >= 1; --i) {
    Serial.printf("ON_START_IN,%d\n", i);
    delay(1000);
  }
  Serial.println(F("GO_ON_NOW"));
  scanPass(onResults, "ON", false);
  Serial.println(F("ON_SCAN_DONE"));
  printCombinedResults();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_CSN, OUTPUT);
  digitalWrite(PIN_CSN, HIGH);
  pinMode(PIN_GDO0, INPUT);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  delay(50);

  resetCC2500();
  configureReceiver();
  attachInterrupt(digitalPinToInterrupt(PIN_GDO0), gdo0EdgeISR, CHANGE);

  Serial.println();
  Serial.println(F("ESP32-S3 + CC2500: skan IDLE i ciagle ON"));
  printChipInfo();
  printMenu();
}

void loop() {
  if (!Serial.available()) {
    delay(10);
    return;
  }

  const char command = (char)Serial.read();
  while (Serial.available()) {
    const char c = (char)Serial.peek();
    if (c == '\r' || c == '\n' || c == ' ' || c == '\t') Serial.read();
    else break;
  }

  if (command == 's' || command == 'S') {
    runFullComparison();
  } else if (command == 'r' || command == 'R') {
    printChipInfo();
  } else if (command == 'h' || command == 'H' || command == '?') {
    printMenu();
  }
}