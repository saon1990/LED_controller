
#include <Arduino.h>
#include <SPI.h>

#include "driver/rmt_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// ESP32-S3 DevKitC-1 + CC2500
static constexpr int PIN_SCK  = 4;
static constexpr int PIN_MOSI = 5;
static constexpr int PIN_MISO = 6;
static constexpr int PIN_CSN  = 7;
static constexpr int PIN_GDO0 = 15;

static constexpr float RF_FREQ_MHZ = 2472.750f;
static constexpr uint32_t CAPTURE_MS = 1000;

// CC2500 commands
static constexpr uint8_t CC_SRES = 0x30;
static constexpr uint8_t CC_SRX  = 0x34;
static constexpr uint8_t CC_SIDLE = 0x36;

// CC2500 registers
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

static SPISettings ccSpiSettings(1000000, MSBFIRST, SPI_MODE0);

static rmt_channel_handle_t rxChannel = nullptr;
static QueueHandle_t rxQueue = nullptr;

static constexpr size_t RMT_BUFFER_SYMBOLS = 128;
static constexpr size_t MAX_CAPTURE_SYMBOLS = 5000;

static rmt_symbol_word_t rmtBuffer[RMT_BUFFER_SYMBOLS];
static rmt_symbol_word_t captured[MAX_CAPTURE_SYMBOLS];
static size_t capturedCount = 0;
static bool captureOverflow = false;

struct RxEvent {
    size_t numSymbols;
    const rmt_symbol_word_t *symbols;
};

static bool IRAM_ATTR onRmtReceive(
    rmt_channel_handle_t channel,
    const rmt_rx_done_event_data_t *edata,
    void *userData)
{
    BaseType_t highPriorityTaskWoken = pdFALSE;

    RxEvent event;
    event.numSymbols = edata->num_symbols;
    event.symbols = edata->received_symbols;

    if (rxQueue != nullptr) {
        xQueueSendFromISR(rxQueue, &event, &highPriorityTaskWoken);
    }

    return highPriorityTaskWoken == pdTRUE;
}

static void ccSelect()
{
    SPI.beginTransaction(ccSpiSettings);
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
    // CC2500 frequency word for a 26 MHz crystal.
    const uint32_t word =
        (uint32_t)((mhz * 1000000.0f * 65536.0f) / 26000000.0f);

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

    // Same RF settings as the previous raw-timing experiment.
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
    Serial.printf("PKTCTRL0: 0x%02X\n", ccReadStatus(REG_PKTCTRL0));
    Serial.printf("IOCFG0: 0x%02X\n", ccReadStatus(REG_IOCFG0));
    Serial.printf("MDMCFG4: 0x%02X\n", ccReadStatus(REG_MDMCFG4));
    Serial.printf("MDMCFG3: 0x%02X\n", ccReadStatus(REG_MDMCFG3));
    Serial.printf("RF frequency: %.3f MHz\n", RF_FREQ_MHZ);
}

static bool setupRMT()
{
    rxQueue = xQueueCreate(1, sizeof(RxEvent));
    if (rxQueue == nullptr) {
        Serial.println("ERROR: Could not create RMT queue");
        return false;
    }

    rmt_rx_channel_config_t channelConfig = {};
    channelConfig.gpio_num = (gpio_num_t)PIN_GDO0;
    channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
    channelConfig.resolution_hz = 1000000; // 1 tick = 1 us
    channelConfig.mem_block_symbols = RMT_BUFFER_SYMBOLS;
    channelConfig.flags.invert_in = false;
    channelConfig.flags.with_dma = false;

    esp_err_t err = rmt_new_rx_channel(&channelConfig, &rxChannel);
    if (err != ESP_OK) {
        Serial.printf("ERROR rmt_new_rx_channel: %s\n",
                      esp_err_to_name(err));
        return false;
    }

    rmt_rx_event_callbacks_t callbacks = {};
    callbacks.on_recv_done = onRmtReceive;

    err = rmt_rx_register_event_callbacks(rxChannel, &callbacks, nullptr);
    if (err != ESP_OK) {
        Serial.printf("ERROR registering RMT callback: %s\n",
                      esp_err_to_name(err));
        return false;
    }

    err = rmt_enable(rxChannel);
    if (err != ESP_OK) {
        Serial.printf("ERROR rmt_enable: %s\n", esp_err_to_name(err));
        return false;
    }

    Serial.println("RMT RX ready: GPIO15, resolution 1 MHz");
    return true;
}


static bool armRMT()
{
    rmt_receive_config_t receiveConfig = {};

    // RMT wymaga wartości poniżej limitu sterownika.
    // 1000 ns = 1 us; nie oznacza to, że będziemy dekodować
    // impulsy 1-us, ponieważ ich interpretacją zajmiemy się później.
    receiveConfig.signal_range_min_ns = 1000;

    // Zakończ odbiór po stabilnym poziomie trwającym 10 ms.
    receiveConfig.signal_range_max_ns = 10000000;

    esp_err_t err = rmt_receive(
        rxChannel,
        rmtBuffer,
        sizeof(rmtBuffer),
        &receiveConfig
    );

    if (err != ESP_OK) {
        Serial.printf(
            "ERROR rmt_receive: %s\n",
            esp_err_to_name(err)
        );
        return false;
    }

    return true;
}

static void appendSymbols(const RxEvent &event)
{
    size_t count = event.numSymbols;

    if (count > RMT_BUFFER_SYMBOLS) {
        count = RMT_BUFFER_SYMBOLS;
    }

    for (size_t i = 0; i < count; i++) {
        if (capturedCount >= MAX_CAPTURE_SYMBOLS) {
            captureOverflow = true;
            return;
        }

        captured[capturedCount++] = event.symbols[i];
    }
}

static void printCapture(char command)
{
    Serial.println();
    Serial.printf("=== RMT CAPTURE '%c' ===\n", command);
    Serial.printf("Symbols: %u\n", (unsigned)capturedCount);
    Serial.printf("Buffer overflow: %s\n",
                  captureOverflow ? "YES" : "NO");

    Serial.println("Index | duration0 level0 | duration1 level1");
    Serial.println("------+------------------+------------------");

    for (size_t i = 0; i < capturedCount; i++) {
        const rmt_symbol_word_t &s = captured[i];

        Serial.printf(
            "%04u | %5u us   %u     | %5u us   %u\n",
            (unsigned)i,
            (unsigned)s.duration0,
            (unsigned)s.level0,
            (unsigned)s.duration1,
            (unsigned)s.level1
        );
    }

    Serial.println("=== END CAPTURE ===");
    Serial.println();
}

static void captureForOneSecond(char command)
{
    capturedCount = 0;
    captureOverflow = false;

    // Drain any stale event before starting.
    RxEvent stale;
    while (xQueueReceive(rxQueue, &stale, 0) == pdTRUE) {
    }

    Serial.printf("Capturing for %lu ms... press/hold '%c' on the remote now.\n",
                  (unsigned long)CAPTURE_MS, command);

    const uint32_t started = millis();
    uint32_t lastArmError = 0;
    bool armed = armRMT();

    while ((uint32_t)(millis() - started) < CAPTURE_MS) {
        RxEvent event;

        if (xQueueReceive(rxQueue, &event, pdMS_TO_TICKS(20)) == pdTRUE) {
            appendSymbols(event);

            if (capturedCount < MAX_CAPTURE_SYMBOLS &&
                (uint32_t)(millis() - started) < CAPTURE_MS) {
                armed = armRMT();
                if (!armed) {
                    lastArmError = millis();
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
            }
        } else if (!armed) {
            if ((uint32_t)(millis() - lastArmError) >= 20) {
                armed = armRMT();
                lastArmError = millis();
            }
        }
    }

    // Stop any pending receive operation cleanly.
    rmt_disable(rxChannel);
    rmt_enable(rxChannel);

    printCapture(command);
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("CC2500 + ESP32-S3 hardware RMT capture");

    setupCC2500();

    if (!setupRMT()) {
        Serial.println("RMT setup failed; stopping.");
        while (true) {
            delay(1000);
        }
    }

    Serial.println();
    Serial.println("Commands:");
    Serial.println("  i = capture idle/no button");
    Serial.println("  o = capture while pressing ON");
    Serial.println("  f = capture while pressing OFF");
    Serial.println();
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
        captureForOneSecond(command);
    } else {
        Serial.println("Unknown command. Use i, o or f.");
    }
}
