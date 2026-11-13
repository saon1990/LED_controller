
#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <string.h>

#include "REMOTE/RemoteProtocol.h"
#include "REMOTE/RemoteCommands.h"
#include "REMOTE/RemoteRadio.h"

constexpr uint8_t NRF_CE   = 15;
constexpr uint8_t NRF_CSN  = 7;
constexpr uint8_t NRF_SCK  = 4;
constexpr uint8_t NRF_MISO = 6;
constexpr uint8_t NRF_MOSI = 5;

constexpr uint8_t NRF_CHANNEL = 10;
constexpr uint8_t RX_LENGTH = 12;

RF24 radio(NRF_CE, NRF_CSN);
uint32_t packetCounter = 0;

static const REMOTE::RemoteAction ACTIONS[] = {
    REMOTE::RemoteAction::ON,
    REMOTE::RemoteAction::OFF,
    REMOTE::RemoteAction::COLOR_CYCLE,
    REMOTE::RemoteAction::SUN_TOP,
    REMOTE::RemoteAction::MODE,
    REMOTE::RemoteAction::O_MINUS,
    REMOTE::RemoteAction::WHITE,
    REMOTE::RemoteAction::O_PLUS,
    REMOTE::RemoteAction::SPEED_MINUS,
    REMOTE::RemoteAction::SUN_BOTTOM,
    REMOTE::RemoteAction::SPEED_PLUS,

    REMOTE::RemoteAction::GROUP_1_TOP,
    REMOTE::RemoteAction::GROUP_1_BOTTOM,
    REMOTE::RemoteAction::GROUP_2_TOP,
    REMOTE::RemoteAction::GROUP_2_BOTTOM,
    REMOTE::RemoteAction::GROUP_3_TOP,
    REMOTE::RemoteAction::GROUP_3_BOTTOM,
    REMOTE::RemoteAction::GROUP_4_TOP,
    REMOTE::RemoteAction::GROUP_4_BOTTOM,

    REMOTE::RemoteAction::WHEEL_RED,
    REMOTE::RemoteAction::WHEEL_ORANGE,
    REMOTE::RemoteAction::WHEEL_YELLOW,
    REMOTE::RemoteAction::WHEEL_GREEN,
    REMOTE::RemoteAction::WHEEL_CYAN,
    REMOTE::RemoteAction::WHEEL_BLUE,
    REMOTE::RemoteAction::WHEEL_PINK
};

uint8_t reverseBits(uint8_t value) {
    value = (value & 0xF0) >> 4 | (value & 0x0F) << 4;
    value = (value & 0xCC) >> 2 | (value & 0x33) << 2;
    value = (value & 0xAA) >> 1 | (value & 0x55) << 1;
    return value;
}

uint16_t calculateRadioCrc(const uint8_t* data, uint8_t length) {
    uint16_t state = 0;

    for (uint8_t i = 0; i < length; i++) {
        uint8_t value = data[i];

        for (uint8_t bit = 0; bit < 8; bit++) {
            if ((value ^ state) & 0x01) {
                state = (state >> 1) ^ 0x8408;
            } else {
                state >>= 1;
            }

            value >>= 1;
        }
    }

    return state;
}

void printHex(uint8_t value) {
    if (value < 0x10) {
        Serial.print('0');
    }

    Serial.print(value, HEX);
}

void printBuffer(const char* label, const uint8_t* data, uint8_t length) {
    Serial.print(label);

    for (uint8_t i = 0; i < length; i++) {
        printHex(data[i]);
        Serial.print(' ');
    }

    Serial.println();
}

const char* identifyCommand(uint8_t command, uint8_t argument, uint8_t group) {
    const uint8_t commandBase = command & 0x7F;

    for (uint8_t a = 0; a < sizeof(ACTIONS) / sizeof(ACTIONS[0]); a++) {
        const REMOTE::RemoteAction action = ACTIONS[a];
        const uint8_t variants = REMOTE::getCommandVariantCount(action);

        for (uint8_t v = 0; v < variants; v++) {
            REMOTE::RemoteCommand variant = {};

            if (!REMOTE::getCommandVariant(action, v, &variant)) {
                continue;
            }

            if ((variant.command & 0x7F) == commandBase &&
                variant.argument == argument &&
                variant.group == group) {
                return REMOTE::getActionName(action);
            }
        }
    }

    return "UNKNOWN";
}

void processPacket(const uint8_t* raw) {
    uint8_t logical[RX_LENGTH] = {};

    for (uint8_t i = 0; i < RX_LENGTH; i++) {
        logical[i] = reverseBits(raw[i]);
    }

    const bool validLength = (logical[0] == 9);
    const uint16_t calculatedCrc = calculateRadioCrc(logical, 10);
    const uint16_t receivedCrc =
        static_cast<uint16_t>(logical[10]) |
        (static_cast<uint16_t>(logical[11]) << 8);

    const bool validRadioCrc = (calculatedCrc == receivedCrc);

    Serial.println();
    Serial.println("========================================");
    Serial.printf("PACKET #%lu\n", static_cast<unsigned long>(++packetCounter));
    Serial.printf("NRF CHANNEL: %u\n", NRF_CHANNEL);

    printBuffer("RAW:      ", raw, RX_LENGTH);
    printBuffer("LOGICAL:  ", logical, RX_LENGTH);

    Serial.printf("LENGTH: %s\n", validLength ? "VALID" : "INVALID");
    Serial.printf("RADIO CRC: %s\n", validRadioCrc ? "VALID" : "INVALID");
    Serial.printf("CRC CALCULATED: 0x%04X\n", calculatedCrc);
    Serial.printf("CRC RECEIVED:   0x%04X\n", receivedCrc);

    if (!validLength || !validRadioCrc) {
        Serial.println("V2 DECODE: SKIPPED");
        Serial.println("========================================");
        return;
    }

    uint8_t decoded[REMOTE::V2_PACKET_SIZE] = {};

    const bool validV2 =
        REMOTE::decodeAndValidateV2Packet(&logical[1], decoded);

    printBuffer("V2 DECODED: ", decoded, REMOTE::V2_PACKET_SIZE);
    Serial.printf("V2 CHECKSUM: %s\n", validV2 ? "VALID" : "INVALID");

    if (!validV2) {
        Serial.println("COMMAND: NOT DECODED");
        Serial.println("========================================");
        return;
    }

    const uint16_t deviceId =
        (static_cast<uint16_t>(decoded[2]) << 8) | decoded[3];

    const bool validHeader =
        decoded[0] == 0xEE &&
        decoded[1] == 0x25 &&
        deviceId == 0x275D;

    Serial.printf("PROTOCOL ID: 0x%02X\n", decoded[1]);
    Serial.printf("DEVICE ID: 0x%04X\n", deviceId);

    if (!validHeader) {
        Serial.println("DEVICE HEADER: UNKNOWN");
        Serial.println("========================================");
        return;
    }

    const uint8_t command = decoded[4];
    const uint8_t argument = decoded[5];
    const uint8_t sequence = decoded[6];
    const uint8_t group = decoded[7];

    const char* action = identifyCommand(command, argument, group);

    Serial.println("----------------------------------------");
    Serial.printf("COMMAND: 0x%02X\n", command);
    Serial.printf("COMMAND BASE: 0x%02X\n", command & 0x7F);
    Serial.printf("HELD FLAG: %s\n", (command & 0x80) ? "YES" : "NO");
    Serial.printf("ARGUMENT: 0x%02X\n", argument);
    Serial.printf("SEQUENCE: 0x%02X\n", sequence);
    Serial.printf("GROUP: 0x%02X\n", group);
    Serial.printf("RECOGNIZED ACTION: %s\n", action);
    Serial.println("========================================");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println(" RC03RFB RECEIVER - RX ONLY");
    Serial.println(" ESP32-S3 + nRF24L01+");
    Serial.println("========================================");

    SPI.begin(NRF_SCK, NRF_MISO, NRF_MOSI, NRF_CSN);

    if (!radio.begin()) {
        Serial.println("[ERROR] NRF24 NOT DETECTED!");

        while (true) {
            delay(1000);
        }
    }

    Serial.println("[NRF24] CHIP DETECTED");

    radio.setPALevel(RF24_PA_MIN);
    radio.setDataRate(RF24_1MBPS);
    radio.setAutoAck(false);
    radio.disableCRC();
    radio.setAddressWidth(5);

    radio.openReadingPipe(1, REMOTE::RADIO_ADDRESS);
    radio.setPayloadSize(RX_LENGTH);
    radio.setChannel(NRF_CHANNEL);

    radio.flush_rx();
    radio.startListening();

    Serial.println("[NRF24] RX MODE ACTIVE");
    Serial.println("[NRF24] AUTO ACK: DISABLED");
    Serial.println("[NRF24] HARDWARE CRC: DISABLED");
    Serial.println("[NRF24] DATA RATE: 1 MBPS");
    Serial.printf("[NRF24] CHANNEL: %u\n", NRF_CHANNEL);

    printBuffer("[NRF24] ADDRESS: ",
                REMOTE::RADIO_ADDRESS,
                REMOTE::RADIO_ADDRESS_SIZE);

    Serial.println();
    Serial.println("Press buttons on RC03RFB.");
    Serial.println("Waiting for packets...");
    Serial.println();
}

void loop() {
    while (radio.available()) {
        uint8_t raw[RX_LENGTH] = {};
        radio.read(raw, RX_LENGTH);
        processPacket(raw);
    }

    delay(1);
}
