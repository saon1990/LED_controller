
#include <Arduino.h>
#include <SPI.h>
#include "driver/gpio.h"

static constexpr int PIN_SCK  = 4;
static constexpr int PIN_MOSI = 5;
static constexpr int PIN_MISO = 6;
static constexpr int PIN_CSN  = 7;
static constexpr int PIN_GDO0 = 15;

static constexpr uint32_t SPI_FREQ = 1000000;
static constexpr uint32_t CAPTURE_MS = 5000;
static constexpr uint32_t ARM_DELAY_MS = 2000;
static constexpr uint32_t MAX_EDGES = 4096;
static constexpr uint32_t PRINT_LIMIT = 120;

SPISettings cc2500SPI(SPI_FREQ, MSBFIRST, SPI_MODE0);

struct Edge {
  uint32_t timeUs;
  uint8_t level;
};

static Edge edgeBuffer[MAX_EDGES];

static volatile uint32_t edgeCount = 0;
static volatile bool captureActive = false;
static volatile bool bufferOverflow = false;
static volatile uint32_t repeatedLevels = 0;

static uint8_t lastLevel = 0;
static portMUX_TYPE edgeMux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR gdo0ISR() {
  const uint8_t level = (uint8_t)gpio_get_level((gpio_num_t)PIN_GDO0);
  const uint32_t timestamp = micros();

  portENTER_CRITICAL_ISR(&edgeMux);

  if (captureActive) {
    uint32_t idx = edgeCount;

    if (idx < MAX_EDGES) {
      edgeBuffer[idx].timeUs = timestamp;
      edgeBuffer[idx].level = level;
      edgeCount = idx + 1;

      if (idx > 0 && level == lastLevel) {
        repeatedLevels++;
      }
      lastLevel = level;
    } else {
      bufferOverflow = true;
    }
  }

  portEXIT_CRITICAL_ISR(&edgeMux);
}

static void ccSelect() {
  SPI.beginTransaction(cc2500SPI);
  digitalWrite(PIN_CSN, LOW);
  delayMicroseconds(1);
}

static void ccDeselect() {
  digitalWrite(PIN_CSN, HIGH);
  SPI.endTransaction();
}

static uint8_t ccReadStatus(uint8_t addr) {
  ccSelect();
  SPI.transfer(addr | 0xC0);
  uint8_t value = SPI.transfer(0x00);
  ccDeselect();
  return value;
}

static void ccWriteReg(uint8_t addr, uint8_t value) {
  ccSelect();
  SPI.transfer(addr);
  SPI.transfer(value);
  ccDeselect();
}

static void ccStrobe(uint8_t command) {
  ccSelect();
  SPI.transfer(command);
  ccDeselect();
}

static void configureCC2500() {
  digitalWrite(PIN_CSN, HIGH);
  delay(1);

  ccSelect();
  ccDeselect();
  delayMicroseconds(5);

  ccSelect();
  SPI.transfer(0x30);  // SRES
  ccDeselect();
  delay(5);

  // Frequency word: 2472.750 MHz, 26 MHz crystal
  ccWriteReg(0x0D, 0x5F);  // FREQ2
  ccWriteReg(0x0E, 0x1B);  // FREQ1
  ccWriteReg(0x0F, 0x14);  // FREQ0

  // Preserve the asynchronous capture configuration
  ccWriteReg(0x02, 0x0D);  // IOCFG0: async serial data
  ccWriteReg(0x08, 0x30);  // PKTCTRL0: async serial mode
  ccWriteReg(0x10, 0x03);  // MDMCFG4
  ccWriteReg(0x11, 0x43);  // MDMCFG3
  ccWriteReg(0x12, 0x00);  // MDMCFG2

  // Other previously used receiver settings
  ccWriteReg(0x00, 0x06);  // IOCFG2
  ccWriteReg(0x01, 0x2E);  // IOCFG1
  ccWriteReg(0x03, 0x47);  // FIFOTHR
  ccWriteReg(0x04, 0xD3);  // SYNC1
  ccWriteReg(0x05, 0x91);  // SYNC0
  ccWriteReg(0x06, 0xFF);  // PKTLEN
  ccWriteReg(0x07, 0x04);  // PKTCTRL1
  ccWriteReg(0x09, 0x00);  // ADDR
  ccWriteReg(0x0A, 0x00);  // CHANNR
  ccWriteReg(0x13, 0x22);  // MDMCFG1
  ccWriteReg(0x14, 0xF8);  // MDMCFG0
  ccWriteReg(0x15, 0x15);  // DEVIATN
  ccWriteReg(0x18, 0x18);  // MCSM0
  ccWriteReg(0x19, 0x16);  // FOCCFG
  ccWriteReg(0x1A, 0x6C);  // BSCFG
  ccWriteReg(0x1B, 0x03);  // AGCCTRL2
  ccWriteReg(0x1C, 0x40);  // AGCCTRL1
  ccWriteReg(0x1D, 0x91);  // AGCCTRL0
  ccWriteReg(0x21, 0x56);  // FREND1
  ccWriteReg(0x22, 0x10);  // FREND0
  ccWriteReg(0x23, 0xE9);  // FSCAL3
  ccWriteReg(0x24, 0x2A);  // FSCAL2
  ccWriteReg(0x25, 0x00);  // FSCAL1
  ccWriteReg(0x26, 0x1F);  // FSCAL0
  ccWriteReg(0x29, 0x59);  // FSTEST
  ccWriteReg(0x2C, 0x88);  // TEST2
  ccWriteReg(0x2D, 0x31);  // TEST1
  ccWriteReg(0x2E, 0x0B);  // TEST0

  ccStrobe(0x33);  // SCAL
  delay(2);
  ccStrobe(0x34);  // SRX
  delay(2);
}

static void captureEdges() {
  portENTER_CRITICAL(&edgeMux);
  edgeCount = 0;
  repeatedLevels = 0;
  bufferOverflow = false;
  lastLevel = (uint8_t)gpio_get_level((gpio_num_t)PIN_GDO0);
  captureActive = true;
  portEXIT_CRITICAL(&edgeMux);

  Serial.println("\nCAPTURE STARTED: press the selected button once.");
  delay(CAPTURE_MS);

  portENTER_CRITICAL(&edgeMux);
  captureActive = false;
  const uint32_t count = edgeCount;
  const uint32_t repeats = repeatedLevels;
  const bool overflow = bufferOverflow;
  portEXIT_CRITICAL(&edgeMux);

  Serial.println("\n--- SUMMARY ---");
  Serial.printf("Edges: %lu\n", count);
  Serial.printf("Repeated consecutive levels: %lu\n", repeats);
  Serial.printf("Buffer overflow: %s\n", overflow ? "YES" : "NO");

  Serial.println("\n--- FIRST EDGE INTERVALS ---");
  Serial.println("index,level,delta_us");

  uint32_t previousTime = 0;
  const uint32_t toPrint = (count < PRINT_LIMIT) ? count : PRINT_LIMIT;

  for (uint32_t i = 0; i < toPrint; i++) {
    const uint32_t timestamp = edgeBuffer[i].timeUs;
    const uint32_t delta =
        (i == 0) ? 0 : (uint32_t)(timestamp - previousTime);

    Serial.printf("%lu,%u,%lu\n",
                  i, edgeBuffer[i].level, delta);

    previousTime = timestamp;
  }

  if (count > PRINT_LIMIT) {
    Serial.printf("Output limited to %lu of %lu edges.\n",
                  PRINT_LIMIT, count);
  }

  Serial.println("--- END ---");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_CSN, OUTPUT);
  digitalWrite(PIN_CSN, HIGH);

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  pinMode(PIN_GDO0, INPUT);

  Serial.println("\nCC2500 capture test");

  configureCC2500();

  Serial.printf("PARTNUM=0x%02X VERSION=0x%02X\n",
                ccReadStatus(0x30), ccReadStatus(0x31));

  attachInterrupt(digitalPinToInterrupt(PIN_GDO0), gdo0ISR, CHANGE);

  Serial.println("Ready. Press Enter to start a capture.");
}

void loop() {
  if (Serial.available() > 0) {
    while (Serial.available() > 0) {
      Serial.read();
    }

    Serial.printf("Capture starts in %lu seconds...\n",
                  ARM_DELAY_MS / 1000);
    delay(ARM_DELAY_MS);
    captureEdges();

    Serial.println("\nPress Enter for the next capture.");
  }
}
