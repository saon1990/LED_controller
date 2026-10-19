
#include <Arduino.h>
#include <SPI.h>

#include "driver/rmt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

// ESP32-S3 DevKitC-1 + CC2500
static constexpr int PIN_SCK  = 4;
static constexpr int PIN_MOSI = 5;
static constexpr int PIN_MISO = 6;
static constexpr int PIN_CSN  = 7;
static constexpr gpio_num_t PIN_GDO0 = GPIO_NUM_15;

static constexpr float RF_FREQ_MHZ = 2472.750f;
static constexpr uint32_t CAPTURE_MS = 2000;

static constexpr rmt_channel_t RMT_CHANNEL = RMT_CHANNEL_0;
static constexpr uint8_t RMT_CLK_DIV = 80; // 80 MHz / 80 = 1 MHz
static constexpr uint16_t RMT_IDLE_TICKS = 10000; // 10 ms

static constexpr size_t MAX_PULSES = 12000;
static constexpr size_t MAX_SEGMENTS = 500;

static constexpr uint8_t CC_SRES  = 0x30;
static constexpr uint8_t CC_SRX   = 0x34;
static constexpr uint8_t CC_SIDLE = 0x36;

static constexpr uint8_t REG_IOCFG0   = 0x02;
static constexpr uint8_t REG_PKTCTRL0 = 0x08;
static constexpr uint8_t REG_FSCTRL1  = 0x0B;
static constexpr uint8_t REG_FREQ2    = 0x0D;
static constexpr uint8_t REG_FREQ1    = 0x0E;
static constexpr uint8_t REG_FREQ0    = 0x0F;
static constexpr uint8_t REG_MDMCFG4  = 0x10;
static constexpr uint8_t REG_MDMCFG3  = 0x11;
static constexpr uint8_t REG_MDMCFG2  = 0x12;
static constexpr uint8_t REG_MDMCFG1  = 0x13;
static constexpr uint8_t REG_MDMCFG0  = 0x14;
static constexpr uint8_t REG_DEVIATN  = 0x15;
static constexpr uint8_t REG_MCSM0    = 0x18;
static constexpr uint8_t REG_AGCCTRL2 = 0x1B;
static constexpr uint8_t REG_AGCCTRL1 = 0x1C;
static constexpr uint8_t REG_AGCCTRL0 = 0x1D;

static constexpr uint8_t REG_PARTNUM = 0x30;
static constexpr uint8_t REG_VERSION = 0x31;

struct Pulse {
    uint16_t us;
    uint8_t level;
};

static SPISettings spiSettings(1000000, MSBFIRST, SPI_MODE0);
static RingbufHandle_t rxRingbuffer = nullptr;

static Pulse pulses[MAX_PULSES];
static size_t pulseCount = 0;

static size_t segmentStarts[MAX_SEGMENTS];
static size_t segmentEnds[MAX_SEGMENTS];
static size_t segmentCount = 0;

static bool pulseOverflow = false;
static bool segmentOverflow = false;

static void ccSelect()
{
    SPI.beginTransaction(spiSettings);
    digitalWrite(PIN_CSN, LOW);
}

static void ccDeselect()
{
    digitalWrite(PIN_CSN, HIGH);
    SPI.endTransaction();
}

static void ccStrobe(uint8_t command)
{
    ccSelect();
    SPI.transfer(command);
    ccDeselect();
}

static void ccWriteReg(uint8_t reg, uint8_t value)
{
    ccSelect();
    SPI.transfer(reg);
    SPI.transfer(value);
    ccDeselect();
}

static uint8_t ccReadStatus(uint8_t reg)
{
    ccSelect();
    SPI.transfer(reg | 0xC0);
    uint8_t value = SPI.transfer(0x00);
    ccDeselect();
    return value;
}

static void ccSetFrequency(float mhz)
{
    uint32_t word = (uint32_t)(
        (mhz * 1000000.0f * 65536.0f) / 26000000.0f
    );

    ccWriteReg(REG_FREQ2, (word >> 16) & 0xFF);
    ccWriteReg(REG_FREQ1, (word >> 8) & 0xFF);
    ccWriteReg(REG_FREQ0, word & 0xFF);
}

static void setupCC2500()
{
    pinMode(PIN_CSN, OUTPUT);
    digitalWrite(PIN_CSN, HIGH);

    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
    delay(50);

    ccStrobe(CC_SRES);
    delay(5);

    // Dotychczasowa konfiguracja odbiornika CC2500.
    ccWriteReg(REG_IOCFG0,   0x0D);
    ccWriteReg(REG_PKTCTRL0, 0x30);
    ccWriteReg(REG_FSCTRL1,  0x0C);

    ccWriteReg(REG_MDMCFG4,  0x03);
    ccWriteReg(REG_MDMCFG3,  0x43);
    ccWriteReg(REG_MDMCFG2,  0x00);
    ccWriteReg(REG_MDMCFG1,  0x22);
    ccWriteReg(REG_MDMCFG0,  0xF8);
    ccWriteReg(REG_DEVIATN,  0x44);
    ccWriteReg(REG_MCSM0,    0x18);

    ccWriteReg(REG_AGCCTRL2, 0xC7);
    ccWriteReg(REG_AGCCTRL1, 0x18);
    ccWriteReg(REG_AGCCTRL0, 0xB0);

    ccSetFrequency(RF_FREQ_MHZ);

    ccStrobe(CC_SIDLE);
    delay(2);
    ccStrobe(CC_SRX);
    delay(10);

    Serial.printf("PARTNUM: 0x%02X\n", ccReadStatus(REG_PARTNUM));
    Serial.printf("VERSION: 0x%02X\n", ccReadStatus(REG_VERSION));
    Serial.printf("RF frequency: %.3f MHz\n", RF_FREQ_MHZ);
}

static bool setupRMT()
{
    rmt_config_t config = {};
    config.rmt_mode = RMT_MODE_RX;
    config.channel = RMT_CHANNEL;
    config.gpio_num = PIN_GDO0;
    config.clk_div = RMT_CLK_DIV;
    config.mem_block_num = 1;

    config.rx_config.filter_en = true;
    config.rx_config.filter_ticks_thresh = 1;
    config.rx_config.idle_threshold = RMT_IDLE_TICKS;

    esp_err_t err = rmt_config(&config);
    if (err != ESP_OK) {
        Serial.printf("rmt_config failed: %s\n", esp_err_to_name(err));
        return false;
    }

    // Bufor FreeRTOS na odebrane fragmenty RMT.
    err = rmt_driver_install(RMT_CHANNEL, 8192, 0);
    if (err != ESP_OK) {
        Serial.printf("rmt_driver_install failed: %s\n",
                      esp_err_to_name(err));
        return false;
    }

    err = rmt_get_ringbuf_handle(RMT_CHANNEL, &rxRingbuffer);
    if (err != ESP_OK || rxRingbuffer == nullptr) {
        Serial.printf("rmt_get_ringbuf_handle failed: %s\n",
                      esp_err_to_name(err));
        return false;
    }

    Serial.println("RMT RX ready: GPIO15, resolution 1 us");
    return true;
}

static void addPulse(uint16_t duration, uint8_t level)
{
    if (duration == 0) return;

    if (pulseCount >= MAX_PULSES) {
        pulseOverflow = true;
        return;
    }

    pulses[pulseCount++] = {duration, level};
}

static void appendRmtItems(const rmt_item32_t *items, size_t count)
{
    if (segmentCount >= MAX_SEGMENTS) {
        segmentOverflow = true;
        return;
    }

    segmentStarts[segmentCount] = pulseCount;

    for (size_t i = 0; i < count; i++) {
        addPulse(items[i].duration0, items[i].level0);
        addPulse(items[i].duration1, items[i].level1);

        if (pulseOverflow) break;
    }

    segmentEnds[segmentCount] = pulseCount;
    segmentCount++;
}

static void drainRingbuffer()
{
    while (true) {
        size_t itemSize = 0;
        void *data = xRingbufferReceive(rxRingbuffer, &itemSize, 0);

        if (data == nullptr) break;

        vRingbufferReturnItem(rxRingbuffer, data);
    }
}

static void printAnalysis(char command)
{
    uint32_t histogram[21] = {};
    uint32_t totalUs = 0;
    uint16_t minUs = 65535;
    uint16_t maxUs = 0;

    for (size_t i = 0; i < pulseCount; i++) {
        uint16_t us = pulses[i].us;

        if (us < minUs) minUs = us;
        if (us > maxUs) maxUs = us;
        totalUs += us;

        size_t bin = (us + 250) / 500;
        if (bin >= 21) bin = 20;

        histogram[bin]++;
    }

    Serial.println();
    Serial.printf("========== ANALYSIS '%c' ==========\n", command);
    Serial.printf("Pulses: %u\n", (unsigned)pulseCount);
    Serial.printf("RMT segments: %u\n", (unsigned)segmentCount);
    Serial.printf("Pulse buffer overflow: %s\n",
                  pulseOverflow ? "YES" : "NO");
    Serial.printf("Segment buffer overflow: %s\n",
                  segmentOverflow ? "YES" : "NO");

    if (pulseCount > 0) {
        Serial.printf(
            "Duration min/max/avg: %u / %u / %lu us\n",
            minUs,
            maxUs,
            (unsigned long)(totalUs / pulseCount)
        );
    }

    Serial.println("\nDuration histogram (nearest 500 us):");

    for (size_t i = 0; i < 21; i++) {
        if (histogram[i] == 0) continue;

        Serial.printf("%5u us : %lu\n",
                      (unsigned)(i * 500),
                      (unsigned long)histogram[i]);
    }

    Serial.println("\nRMT segments:");

    for (size_t i = 0; i < segmentCount; i++) {
        size_t first = segmentStarts[i];
        size_t last = segmentEnds[i];

        Serial.printf("SEG %03u: %u pulses | ",
                      (unsigned)i,
                      (unsigned)(last - first));

        size_t shown = last - first;
        if (shown > 30) shown = 30;

        for (size_t j = 0; j < shown; j++) {
            const Pulse &p = pulses[first + j];

            Serial.printf("%u%c",
                          p.us,
                          p.level ? 'H' : 'L');

            if (j + 1 < shown) Serial.print(' ');
        }

        if (last - first > shown) Serial.print(" ...");
        Serial.println();
    }

    Serial.println("========== END ANALYSIS ==========");
    Serial.println();
}

static void captureCommand(char command)
{
    pulseCount = 0;
    segmentCount = 0;
    pulseOverflow = false;
    segmentOverflow = false;

    drainRingbuffer();

    Serial.printf(
        "\nCapturing '%c' for %lu ms. Hold the remote button now.\n",
        command,
        (unsigned long)CAPTURE_MS
    );

    esp_err_t err = rmt_rx_start(RMT_CHANNEL, true);
    if (err != ESP_OK) {
        Serial.printf("rmt_rx_start failed: %s\n", esp_err_to_name(err));
        return;
    }

    const uint32_t started = millis();

    while ((uint32_t)(millis() - started) < CAPTURE_MS &&
           !pulseOverflow &&
           !segmentOverflow) {

        size_t itemSize = 0;

        // Odbieramy kolejne fragmenty zakończone przerwą na GDO0.
        void *data = xRingbufferReceive(
            rxRingbuffer,
            &itemSize,
            pdMS_TO_TICKS(20)
        );

        if (data == nullptr) continue;

        if (itemSize % sizeof(rmt_item32_t) == 0) {
            const size_t count = itemSize / sizeof(rmt_item32_t);

            appendRmtItems(
                static_cast<const rmt_item32_t *>(data),
                count
            );
        } else {
            Serial.printf("Warning: unexpected RMT buffer size: %u\n",
                          (unsigned)itemSize);
        }

        vRingbufferReturnItem(rxRingbuffer, data);
    }

    rmt_rx_stop(RMT_CHANNEL);

    // Opróżnij ewentualny ostatni fragment, który dotarł przy zatrzymaniu.
    while (true) {
        size_t itemSize = 0;
        void *data = xRingbufferReceive(rxRingbuffer, &itemSize, 0);

        if (data == nullptr) break;

        if (itemSize % sizeof(rmt_item32_t) == 0 &&
            !pulseOverflow &&
            !segmentOverflow) {

            appendRmtItems(
                static_cast<const rmt_item32_t *>(data),
                itemSize / sizeof(rmt_item32_t)
            );
        }

        vRingbufferReturnItem(rxRingbuffer, data);
    }

    printAnalysis(command);
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("\nCC2500 + ESP32-S3 legacy RMT analyzer");

    setupCC2500();

    if (!setupRMT()) {
        Serial.println("Initialization failed.");
        while (true) delay(1000);
    }

    Serial.println("\nCommands:");
    Serial.println("  i = idle");
    Serial.println("  o = ON (hold during capture)");
    Serial.println("  f = OFF (hold during capture)");
}

void loop()
{
    if (!Serial.available()) {
        delay(10);
        return;
    }

    char command = (char)Serial.read();

    if (command == '\r' || command == '\n' || command == ' ') {
        return;
    }

    if (command == 'i' || command == 'o' || command == 'f') {
        captureCommand(command);
    } else {
        Serial.println("Unknown command. Use i, o or f.");
    }
}
