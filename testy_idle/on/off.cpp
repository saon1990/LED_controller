#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>

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
int currentFrequency = 1; // Start at the promising 2441 MHz

// CC2500 strobes
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
constexpr uint8_t MARCSTATE = 0x35;

// GDO0_CFG=0x0D: serial data output in asynchronous serial mode.
// PKT_FORMAT=3 in PKTCTRL0 selects asynchronous serial mode.
constexpr uint8_t GDO0_ASYNC_DATA = 0x0D;

// Edge capture uses a circular buffer. We print only the latest edges
// to keep the serial log manageable.
constexpr uint32_t CAPTURE_MS = 5000;
constexpr size_t EDGE_BUFFER_SIZE = 4096;
constexpr size_t PRINT_LAST_EDGES = 300;

struct EdgeRecord {
  uint32_t timeUs;
  uint8_t level;
};

volatile EdgeRecord edgeBuffer[EDGE_BUFFER_SIZE];
volatile uint32_t edgeWritePos = 0;
volatile uint32_t edgeTotal = 0;
volatile uint32_t captureStartUs = 0;
volatile bool captureActive = false;

void IRAM_ATTR gdo0EdgeISR() {
  if (!captureActive) return;

  const uint32_t pos = edgeWritePos;
  edgeBuffer[pos].timeUs = (uint32_t)(micros() - captureStartUs);
  edgeBuffer[pos].level = (uint8_t)gpio_get_level((gpio_num_t)PIN_GDO0);
  edgeWritePos = (pos + 1) % EDGE_BUFFER_SIZE;
  edgeTotal++;
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

  // Asynchronous serial mode. The FIFO is not used for the data stream.
  writeRegister(PKTCTRL0, 0x32);

  writeRegister(FSCTRL1, 0x0C);
  writeRegister(FSCTRL0, 0x00);

  // Keep the same broad diagnostic receiver settings used in the RSSI tests.
  // These are not yet confirmed to match the pilot's modulation/data rate.
  writeRegister(MDMCFG4, 0x03);
  writeRegister(MDMCFG3, 0x93);
  writeRegister(MDMCFG2, 0x00);
  writeRegister(MDMCFG1, 0x22);
  writeRegister(MDMCFG0, 0xF8);

  writeRegister(MCSM0, 0x18);
  writeRegister(AGCCTRL2, 0xC7);
  writeRegister(AGCCTRL1, 0x18);
  writeRegister(AGCCTRL0, 0xB0);

  // Select raw demodulated data on GDO0 (GPIO15).
  writeRegister(IOCFG0, GDO0_ASYNC_DATA);
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

void printMenu() {
  Serial.println();
  Serial.println("====== CC2500 ASYNC DATA EDGE CAPTURE ======");
  Serial.println("1 - ustaw 2410.000 MHz");
  Serial.println("2 - ustaw 2441.000 MHz");
  Serial.println("3 - ustaw 2472.500 MHz");
  Serial.println("i - przechwyc IDLE (nie dotykaj pilota)");
  Serial.println("o - przechwyc transmisje przy przycisku ON");
  Serial.println("f - przechwyc transmisje przy przycisku OFF");
  Serial.println("h - wyswietl menu");
  Serial.println("GDO0 GPIO15 = surowe dane z demodulatora, tryb asynchroniczny.");
  Serial.println("============================================");
}

void captureEdges(char phase) {
  const char* phaseName = (phase == 'i') ? "IDLE" : (phase == 'o') ? "ON" : "OFF";

  captureActive = false;
  edgeWritePos = 0;
  edgeTotal = 0;

  Serial.println();
  Serial.printf("FAZA=%s | FREQ=%.3f MHz\n", phaseName, frequenciesMHz[currentFrequency]);
  Serial.println("Za 3 sekundy rozpocznie sie przechwytywanie na 5 sekund.");
  if (phase == 'i') {
    Serial.println("Nie dotykaj pilota podczas pomiaru.");
  } else {
    Serial.println("Podczas pomiaru wielokrotnie naciskaj i przytrzymuj wskazany przycisk.");
  }

  for (int i = 3; i >= 1; --i) {
    Serial.printf("%d...\n", i);
    delay(1000);
  }

  strobe(SRX);
  delay(10);

  noInterrupts();
  edgeWritePos = 0;
  edgeTotal = 0;
  captureStartUs = micros();
  captureActive = true;
  interrupts();
  const uint32_t startMs = millis();
  while ((uint32_t)(millis() - startMs) < CAPTURE_MS) {
    delay(1);
  }

  noInterrupts();
  captureActive = false;
  const uint32_t total = edgeTotal;
  const uint32_t writePos = edgeWritePos;
  interrupts();

  Serial.println();
  Serial.printf("Zarejestrowane zbocza lacznie: %lu\n", (unsigned long)total);
  Serial.printf("MARCSTATE po pomiarze: 0x%02X\n", readStatus(MARCSTATE) & 0x1F);
  Serial.println("Ostatnie zbocza: czas_us od startu pomiaru, delta_us, poziom");

  uint32_t available = (total < EDGE_BUFFER_SIZE) ? total : EDGE_BUFFER_SIZE;
  uint32_t toPrint = (available < PRINT_LAST_EDGES) ? available : PRINT_LAST_EDGES;
  uint32_t startPos = (writePos + EDGE_BUFFER_SIZE - toPrint) % EDGE_BUFFER_SIZE;

  uint32_t previousTime = 0;
  bool havePrevious = false;
  for (uint32_t i = 0; i < toPrint; ++i) {
    uint32_t pos = (startPos + i) % EDGE_BUFFER_SIZE;
    uint32_t t;
    uint8_t level;

    noInterrupts();
    t = edgeBuffer[pos].timeUs;
    level = edgeBuffer[pos].level;
    interrupts();

    uint32_t delta = havePrevious ? (uint32_t)(t - previousTime) : 0;
    Serial.printf("%lu, %lu, %u\n",
                  (unsigned long)t,
                  (unsigned long)delta,
                  (unsigned int)level);
    previousTime = t;
    havePrevious = true;
  }

  if (total > EDGE_BUFFER_SIZE) {
    Serial.printf("UWAGA: bufor kolowy nadpisal starsze zbocza (%lu pominietych).\n",
                  (unsigned long)(total - EDGE_BUFFER_SIZE));
  }
  Serial.println("Koniec przechwytywania.");
  printMenu();
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(PIN_CSN, OUTPUT);
  digitalWrite(PIN_CSN, HIGH);
  pinMode(PIN_GDO0, INPUT);

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);

  Serial.println();
  Serial.println("ESP32-S3 + CC2500 - ASYNC DATA EDGE CAPTURE");

  resetCC2500();
  Serial.printf("PARTNUM=0x%02X\n", readStatus(PARTNUM));
  Serial.printf("VERSION=0x%02X\n", readStatus(VERSION));

  configureReceiver();
  setFrequency(frequenciesMHz[currentFrequency]);

  // Install the GPIO interrupt once, rather than detaching/reattaching per capture.
  attachInterrupt(digitalPinToInterrupt(PIN_GDO0), gdo0EdgeISR, CHANGE);

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
    captureEdges(phase);
  } else if (cmd == 'h' || cmd == 'H') {
    printMenu();
  } else {
    Serial.println("Nieznana komenda. Wpisz h, aby wyswietlic menu.");
  }
}
