
#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <string.h>

#include "REMOTE/RemoteProtocol.h"
#include "REMOTE/RemoteCommands.h"
#include "REMOTE/RemoteRadio.h"

// --------------------------------------------------
// NRF24 PINS
// --------------------------------------------------

#define NRF_CE   15
#define NRF_CSN  7
#define NRF_SCK  4
#define NRF_MISO 6
#define NRF_MOSI 5

RF24 radio(NRF_CE, NRF_CSN);

const uint8_t NRF_CHANNEL = 10;
const uint8_t RX_LENGTH = 12;

const uint8_t address[5] = {
    0x8A, 0x01, 0xE9, 0xC4, 0x56
};

// --------------------------------------------------
// GLOBAL VARIABLES
// --------------------------------------------------

char actionLabel[32] = "";
uint32_t packetCounter = 0;

// Explicit serial command mode. RF TX is never triggered at boot.
static bool txCommandMode = false;
static char txCommandBuffer[24] = {};
static uint8_t txCommandLength = 0;

// Forward declaration: the self-test implementation is below.
static bool runRemoteProtocolSelfTest();
static bool transmitOneShotOffTest();

// --------------------------------------------------
// MENU
// --------------------------------------------------

void printMenu() {
    Serial.println();
    Serial.println("========================================");
    Serial.println("       RC03RFB BUTTON LABEL MENU");
    Serial.println("========================================");

    Serial.println("[A] ON");
    Serial.println("[B] OFF");

    Serial.println();
    Serial.println("--- COLOR / LIGHT ---");
    Serial.println("[C] COLOR_CYCLE");
    Serial.println("[D] SUN_TOP");
    Serial.println("[E] MODE");
    Serial.println("[F] O_MINUS");
    Serial.println("[G] WHITE");
    Serial.println("[H] O_PLUS");
    Serial.println("[I] SPEED_MINUS");
    Serial.println("[J] SUN_BOTTOM");
    Serial.println("[K] SPEED_PLUS");

    Serial.println();
    Serial.println("--- GROUP BUTTONS ---");
    Serial.println("[L] GROUP_1_TOP");
    Serial.println("[M] GROUP_1_BOTTOM");
    Serial.println("[N] GROUP_2_TOP");
    Serial.println("[O] GROUP_2_BOTTOM");
    Serial.println("[P] GROUP_3_TOP");
    Serial.println("[Q] GROUP_3_BOTTOM");
    Serial.println("[R] GROUP_4_TOP");
    Serial.println("[S] GROUP_4_BOTTOM");

    Serial.println();
    Serial.println("--- COLOR WHEEL ---");
    Serial.println("[T] WHEEL_RED");
    Serial.println("[U] WHEEL_ORANGE");
    Serial.println("[V] WHEEL_YELLOW");
    Serial.println("[W] WHEEL_GREEN");
    Serial.println("[X] WHEEL_CYAN");
    Serial.println("[Y] WHEEL_BLUE");
    Serial.println("[Z] WHEEL_PURPLE");

    Serial.println();
    Serial.println("[0] PAUSE PACKET LOGGING");
    Serial.println("[!TXOFF + Enter] SEND ONE OFF FRAME ON EACH CHANNEL (10/41/72)");

    Serial.println("========================================");
    Serial.println("Press ONE key to select a label.");
    Serial.println("No Enter required.");
    Serial.println("========================================");
}

// --------------------------------------------------
// SET LABEL
// --------------------------------------------------

void setLabel(const char* label) {
    strncpy(
        actionLabel,
        label,
        sizeof(actionLabel) - 1
    );

    actionLabel[sizeof(actionLabel) - 1] = '\0';

    Serial.println();
    Serial.println("----------------------------------------");

    Serial.print(">>> ACTIVE LABEL: ");
    Serial.println(actionLabel);

    Serial.println(">>> PRESS THE CORRESPONDING REMOTE BUTTON");

    Serial.println("----------------------------------------");

    printMenu();

}

// --------------------------------------------------
// SERIAL INPUT
// --------------------------------------------------

void processSerial() {
    while (Serial.available()) {
        char key = static_cast<char>(Serial.read());

        // After '!' we accept one explicit line command. This avoids conflicts
        // with the existing single-key scanner menu.
        if (txCommandMode) {
            if (key == '\r' || key == '\n') {
                txCommandBuffer[txCommandLength] = '\0';
                if (strcmp(txCommandBuffer, "TXOFF") == 0) {
                    Serial.println();
                    Serial.println("EXPLICIT TX COMMAND ACCEPTED: TXOFF");
                    transmitOneShotOffTest();
                } else if (txCommandLength > 0) {
                    Serial.print("UNKNOWN TX COMMAND: ");
                    Serial.println(txCommandBuffer);
                    Serial.println("Expected exactly: !TXOFF then Enter");
                }
                txCommandMode = false;
                txCommandLength = 0;
                txCommandBuffer[0] = '\0';
                continue;
            }

            if (key == 0x1B) { // ESC cancels the pending command
                txCommandMode = false;
                txCommandLength = 0;
                txCommandBuffer[0] = '\0';
                Serial.println("TX COMMAND CANCELLED");
                continue;
            }

            if (txCommandLength < sizeof(txCommandBuffer) - 1) {
                txCommandBuffer[txCommandLength++] = key;
            } else {
                txCommandMode = false;
                txCommandLength = 0;
                txCommandBuffer[0] = '\0';
                Serial.println("TX COMMAND TOO LONG - CANCELLED");
            }
            continue;
        }

        if (key == '\r' || key == '\n') {
            continue;
        }

        if (key == '!') {
            txCommandMode = true;
            txCommandLength = 0;
            txCommandBuffer[0] = '\0';
            Serial.println();
            Serial.println("TX COMMAND MODE");
            Serial.println("Type TXOFF and press Enter to transmit one OFF frame on each channel.");
            Serial.println("Press ESC to cancel.");
            continue;
        }

        if (key >= 'a' && key <= 'z') {
            key = key - ('a' - 'A');
        }

        switch (key) {
            case 'A': setLabel("ON"); break;
            case 'B': setLabel("OFF"); break;
            case 'C': setLabel("COLOR_CYCLE"); break;
            case 'D': setLabel("SUN_TOP"); break;
            case 'E': setLabel("MODE"); break;
            case 'F': setLabel("O_MINUS"); break;
            case 'G': setLabel("WHITE"); break;
            case 'H': setLabel("O_PLUS"); break;
            case 'I': setLabel("SPEED_MINUS"); break;
            case 'J': setLabel("SUN_BOTTOM"); break;
            case 'K': setLabel("SPEED_PLUS"); break;
            case 'L': setLabel("GROUP_1_TOP"); break;
            case 'M': setLabel("GROUP_1_BOTTOM"); break;
            case 'N': setLabel("GROUP_2_TOP"); break;
            case 'O': setLabel("GROUP_2_BOTTOM"); break;
            case 'P': setLabel("GROUP_3_TOP"); break;
            case 'Q': setLabel("GROUP_3_BOTTOM"); break;
            case 'R': setLabel("GROUP_4_TOP"); break;
            case 'S': setLabel("GROUP_4_BOTTOM"); break;
            case 'T': setLabel("WHEEL_RED"); break;
            case 'U': setLabel("WHEEL_ORANGE"); break;
            case 'V': setLabel("WHEEL_YELLOW"); break;
            case 'W': setLabel("WHEEL_GREEN"); break;
            case 'X': setLabel("WHEEL_CYAN"); break;
            case 'Y': setLabel("WHEEL_BLUE"); break;
            case 'Z': setLabel("WHEEL_PURPLE"); break;
            case '0':
                actionLabel[0] = '\0';
                Serial.println();
                Serial.println(">>> PACKET LOGGING PAUSED");
                printMenu();
                break;
            default:
                Serial.print("UNKNOWN MENU KEY: ");
                Serial.println(key);
                break;
        }
    }
}

// --------------------------------------------------
// BIT REVERSE
// --------------------------------------------------

uint8_t reverseBits(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;

    return b;
}

// --------------------------------------------------
// CRC16 - EXTERNAL RADIO CRC
// --------------------------------------------------

uint16_t calcCRC(const uint8_t* data, uint8_t len) {
    uint16_t state = 0;

    for (uint8_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        for (uint8_t j = 0; j < 8; j++) {
            if ((b ^ state) & 0x01) {
                state = (state >> 1) ^ 0x8408;
            } else {
                state >>= 1;
            }

            b >>= 1;
        }
    }

    return state;
}

// --------------------------------------------------
// HEX PRINT
// --------------------------------------------------

void printHex(uint8_t value) {
    if (value < 0x10) {
        Serial.print("0");
    }

    Serial.print(value, HEX);
}

// --------------------------------------------------
// REMOTE RADIO SELF-TEST (NO RF TRANSMISSION)
// --------------------------------------------------

static uint8_t calculateDecodedV2Checksum(const uint8_t* packet) {
    uint8_t sum = REMOTE::calculateXorKey(packet[0]);
    for (uint8_t i = 1; i <= 7; ++i) {
        sum = static_cast<uint8_t>(sum + packet[i]);
    }
    return static_cast<uint8_t>(sum + 2);
}

static bool runRemoteProtocolSelfTest() {
    static const REMOTE::RemoteAction actions[] = {
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
        REMOTE::RemoteAction::WHEEL_PURPLE
    };

    uint16_t tested = 0;
    uint16_t radioFramesTested = 0;
    uint16_t failed = 0;

    Serial.println();
    Serial.println("========================================");
    Serial.println(" REMOTE RADIO SELF-TEST (DRY RUN)");
    Serial.println(" RF TRANSMISSION: DISABLED");
    Serial.println("========================================");

    for (uint8_t a = 0; a < sizeof(actions) / sizeof(actions[0]); ++a) {
        const REMOTE::RemoteAction action = actions[a];
        const uint8_t variants = REMOTE::getCommandVariantCount(action);

        for (uint8_t v = 0; v < variants; ++v) {
            REMOTE::RemoteCommand command = {};
            uint8_t decoded[REMOTE::V2_PACKET_SIZE] = {
                0xEE, 0x25, 0x27, 0x5D, 0x00, 0x00, 0x01, 0x00, 0x00
            };
            uint8_t encoded[REMOTE::V2_PACKET_SIZE] = {};
            uint8_t roundTrip[REMOTE::V2_PACKET_SIZE] = {};
            REMOTE::RadioFrame frame = {};

            ++tested;
            bool ok = REMOTE::getCommandVariant(action, v, &command);

            if (ok) {
                decoded[4] = command.command;
                decoded[5] = command.argument;
                decoded[7] = command.group;
                decoded[8] = calculateDecodedV2Checksum(decoded);
                ok = REMOTE::validateV2Checksum(decoded);
            }

            if (ok) {
                REMOTE::encodeV2Packet(decoded, encoded);
                REMOTE::decodeV2Packet(encoded, roundTrip);
                ok = REMOTE::validateV2Checksum(roundTrip) &&
                     (memcmp(decoded, roundTrip, REMOTE::V2_PACKET_SIZE) == 0);
            }

            if (ok) {
                const uint8_t channelIndex = static_cast<uint8_t>(
                    radioFramesTested % REMOTE::RADIO_CHANNEL_COUNT);
                ok = REMOTE::buildRadioFrame(command, decoded[6], channelIndex, &frame) &&
                     REMOTE::validateRadioFrame(frame);
            }

            if (ok) {
                ++radioFramesTested;
            } else {
                ++failed;
                Serial.print("FAIL: ");
                Serial.print(REMOTE::getActionName(action));
                Serial.print(" variant ");
                Serial.println(v);
            }
        }
    }

    // Captured OFF frame from the user's scanner log, sequence 0x04, nRF24 channel 10.
    const REMOTE::RemoteCommand offCommand = { 0x01, 0x09, 0x00 };
    const uint8_t capturedNrfPayload[REMOTE::RADIO_PAYLOAD_SIZE] = {
        0x90, 0x77, 0x9D, 0x51, 0xE6, 0x79,
        0x9E, 0xAF, 0xBA, 0x8F, 0x79, 0x9F
    };
    REMOTE::RadioFrame capturedFrame = {};
    const bool captureBuilt = REMOTE::buildRadioFrame(offCommand, 0x04, 0, &capturedFrame);
    const bool captureMatch = captureBuilt &&
        capturedFrame.channel == 10 &&
        memcmp(capturedFrame.payload, capturedNrfPayload, REMOTE::RADIO_PAYLOAD_SIZE) == 0 &&
        REMOTE::validateRadioFrame(capturedFrame);

    if (!captureMatch) {
        ++failed;
        Serial.println("FAIL: captured OFF radio frame mismatch");
    }

    const bool channelsValid =
        REMOTE::RADIO_NRF_CHANNELS[0] == 10 &&
        REMOTE::RADIO_NRF_CHANNELS[1] == 41 &&
        REMOTE::RADIO_NRF_CHANNELS[2] == 72;
    if (!channelsValid) {
        ++failed;
        Serial.println("FAIL: radio channel map mismatch");
    }

    Serial.print("COMMAND VARIANTS TESTED: ");
    Serial.println(tested);
    Serial.print("RADIO FRAMES BUILT/VALIDATED: ");
    Serial.println(radioFramesTested);
    Serial.print("CAPTURED OFF FRAME: ");
    Serial.println(captureMatch ? "EXACT MATCH" : "MISMATCH");
    Serial.print("CHANNEL MAP 10/41/72: ");
    Serial.println(channelsValid ? "PASS" : "FAIL");
    Serial.print("FAILURES: ");
    Serial.println(failed);
    Serial.println(failed == 0 ? "REMOTE RADIO SELF-TEST: PASS" : "REMOTE RADIO SELF-TEST: FAIL");
    Serial.println("========================================");

    return failed == 0;
}

// --------------------------------------------------
// NRF24 TRANSMIT PROFILE SELF-TEST (NO RF TRANSMISSION)
// --------------------------------------------------

static bool runRemoteRadioHardwareSelfTest() {
    Serial.println();
    Serial.println("========================================");
    Serial.println(" REMOTE NRF24 TX PROFILE SELF-TEST");
    Serial.println(" RF TRANSMISSION: DISABLED");
    Serial.println("========================================");

    uint8_t passed = 0;
    uint8_t failed = 0;

    for (uint8_t i = 0; i < REMOTE::RADIO_CHANNEL_COUNT; ++i) {
        const uint8_t channel = REMOTE::RADIO_NRF_CHANNELS[i];
        const bool ok = REMOTE::configureTransmitProfile(radio, channel);

        Serial.print("TX PROFILE CHANNEL ");
        Serial.print(channel);
        Serial.print(": ");
        Serial.println(ok ? "PASS" : "FAIL");

        if (ok) {
            ++passed;
        } else {
            ++failed;
        }
    }

    // Return immediately to the scanner's original receive configuration.
    REMOTE::restoreReceiveProfile(radio, NRF_CHANNEL, address, RX_LENGTH);

    const bool rxRestored = radio.isChipConnected() &&
                            radio.getChannel() == NRF_CHANNEL &&
                            radio.getDataRate() == RF24_1MBPS &&
                            radio.getCRCLength() == RF24_CRC_DISABLED;

    Serial.print("TX CHANNEL PROFILES PASSED: ");
    Serial.println(passed);
    Serial.print("TX CHANNEL PROFILE FAILURES: ");
    Serial.println(failed);
    Serial.print("RX PROFILE RESTORE: ");
    Serial.println(rxRestored ? "PASS" : "FAIL");

    if (!rxRestored) {
        ++failed;
    }

    Serial.print("FAILURES: ");
    Serial.println(failed);
    Serial.println(failed == 0 ?
        "REMOTE NRF24 TX PROFILE TEST: PASS" :
        "REMOTE NRF24 TX PROFILE TEST: FAIL");
    Serial.println("========================================");

    return failed == 0;
}

// --------------------------------------------------
// EXPLICIT ONE-SHOT OFF TRANSMISSION TEST
// --------------------------------------------------

static bool transmitOneShotOffTest() {
    // Last supplied capture showed sequence 0x05. Use the next value as a
    // practical first attempt; this is an inference from the observed counter.
    static const uint8_t TEST_SEQUENCE = 0x06;
    static const REMOTE::RemoteCommand OFF_COMMAND = { 0x01, 0x09, 0x00 };

    Serial.println();
    Serial.println("========================================");
    Serial.println(" REMOTE ONE-SHOT TX TEST");
    Serial.println(" COMMAND: OFF");
    Serial.println(" SEQUENCE: 0x06 (next after last observed 0x05)");
    Serial.println(" FRAMES: 1 PER CHANNEL");
    Serial.println("========================================");

    uint8_t successCount = 0;
    uint8_t failureCount = 0;

    for (uint8_t i = 0; i < REMOTE::RADIO_CHANNEL_COUNT; ++i) {
        REMOTE::RadioFrame frame = {};
        const bool built = REMOTE::buildRadioFrame(
            OFF_COMMAND, TEST_SEQUENCE, i, &frame);

        if (!built || !REMOTE::validateRadioFrame(frame)) {
            ++failureCount;
            Serial.print("FRAME BUILD/VALIDATION CHANNEL ");
            Serial.print(REMOTE::RADIO_NRF_CHANNELS[i]);
            Serial.println(": FAIL");
            continue;
        }

        const bool configured = REMOTE::configureTransmitProfile(
            radio, REMOTE::RADIO_NRF_CHANNELS[i]);

        if (!configured) {
            ++failureCount;
            Serial.print("TX PROFILE CHANNEL ");
            Serial.print(frame.channel);
            Serial.println(": FAIL - SKIPPING WRITE");
            continue;
        }

        radio.flush_tx();
        Serial.print("TX CHANNEL ");
        Serial.print(frame.channel);
        Serial.print(" PAYLOAD: ");
        for (uint8_t b = 0; b < REMOTE::RADIO_PAYLOAD_SIZE; ++b) {
            printHex(frame.payload[b]);
            Serial.print(' ');
        }

        // This is the only physical RF transmission call in this stage.
        // Exactly one 12-byte payload is attempted on each of channels 10/41/72.
        const bool txResult = radio.write(
            frame.payload, REMOTE::RADIO_PAYLOAD_SIZE);

        Serial.print("\nTX RESULT CHANNEL ");
        Serial.print(frame.channel);
        Serial.print(": ");
        Serial.println(txResult ? "WRITE_OK" : "WRITE_FAILED");

        if (txResult) {
            ++successCount;
        } else {
            ++failureCount;
        }

        delay(10);
    }

    REMOTE::restoreReceiveProfile(radio, NRF_CHANNEL, address, RX_LENGTH);
    const bool rxRestored = radio.isChipConnected() &&
                            radio.getChannel() == NRF_CHANNEL &&
                            radio.getDataRate() == RF24_1MBPS &&
                            radio.getCRCLength() == RF24_CRC_DISABLED;

    Serial.print("TX WRITE SUCCESSES: ");
    Serial.println(successCount);
    Serial.print("TX WRITE FAILURES: ");
    Serial.println(failureCount);
    Serial.print("RX PROFILE RESTORE: ");
    Serial.println(rxRestored ? "PASS" : "FAIL");

    if (!rxRestored) {
        ++failureCount;
    }

    Serial.print("TOTAL FAILURES: ");
    Serial.println(failureCount);
    Serial.println(failureCount == 0 ?
        "REMOTE ONE-SHOT TX TEST: WRITE_OK" :
        "REMOTE ONE-SHOT TX TEST: CHECK_REQUIRED");
    Serial.println("NOTE: WRITE_OK confirms the radio write operation, not controller reception.");
    Serial.println("========================================");

    return failureCount == 0;
}

// --------------------------------------------------
// MILIGHT V2 DECODER
// --------------------------------------------------

const uint8_t V2_OFFSETS[8][4] = {
    {0x45, 0x1F, 0x14, 0x5C},
    {0x2B, 0xC9, 0xE3, 0x11},
    {0x6D, 0x5F, 0x8A, 0x2B},
    {0xAF, 0x03, 0x1D, 0xF3},
    {0x1A, 0xE2, 0xF0, 0xD1},
    {0x04, 0xD8, 0x71, 0x42},
    {0xAF, 0x04, 0xDD, 0x07},
    {0x61, 0x13, 0x38, 0x64}
};

uint8_t xorKey(uint8_t key) {
    const uint8_t shift = (key & 0x0F) < 0x04 ? 0 : 1;

    const uint8_t x =
        (((key & 0xF0) >> 4) + shift + 6) % 8;

    const uint8_t msn =
        (((4 + x) ^ 1) & 0x0F) << 4;

    const uint8_t lsn =
        ((((key & 0x0F) + 4) ^ 2) & 0x0F);

    return msn | lsn;
}

bool decodeV2Packet(uint8_t* packet) {
    const uint8_t key = xorKey(packet[0]);

    for (uint8_t i = 1; i <= 8; i++) {

        uint8_t offset = V2_OFFSETS[i - 1][packet[0] % 4];

        if (packet[0] >= 0x54 && packet[0] < 0xD4) {
            offset += 0x80;
        }

        packet[i] = packet[i] - offset;
        packet[i] ^= key;
    }

    return true;
}

// --------------------------------------------------
// V2 INTERNAL CHECKSUM
// --------------------------------------------------

bool validateV2Checksum(const uint8_t* packet) {
    uint8_t sum = xorKey(packet[0]);

    for (uint8_t i = 1; i <= 7; i++) {
        sum += packet[i];
    }

    // V2 encoder uses s1 = 2 for checksum byte.
    // Decoder therefore returns sum + 2.

    const uint8_t expected = sum + 2;

    return packet[8] == expected;
}

// --------------------------------------------------
// V2 COMMAND OUTPUT
// --------------------------------------------------

void printV2Packet(const uint8_t* packet) {
    Serial.println();
    Serial.println("---------- MILIGHT V2 DECODE ----------");

    Serial.print("V2 KEY:       0x");
    printHex(packet[0]);

    Serial.print("\nPROTOCOL ID:  0x");
    printHex(packet[1]);

    Serial.print("\nDEVICE ID:    0x");
    printHex(packet[2]);
    printHex(packet[3]);

    Serial.print("\nCOMMAND:      0x");
    printHex(packet[4]);

    Serial.print("\nCOMMAND BASE: 0x");
    printHex(packet[4] & 0x7F);

    Serial.print("\nHELD FLAG:    ");
    Serial.println((packet[4] & 0x80) ? "YES" : "NO");

    Serial.print("ARGUMENT:     0x");
    printHex(packet[5]);

    Serial.print("\nSEQUENCE:     0x");
    printHex(packet[6]);

    Serial.print("\nGROUP:        0x");
    printHex(packet[7]);

    Serial.print("\nV2 CHECKSUM:  0x");
    printHex(packet[8]);

    Serial.print("\nV2 CHECKSUM STATUS: ");
    Serial.println(validateV2Checksum(packet) ? "VALID" : "INVALID");

    Serial.println("---------------------------------------");
}

// --------------------------------------------------
// PACKET PROCESSING
// --------------------------------------------------

void printPacket(uint8_t* raw) {
    uint8_t decoded[RX_LENGTH] = {};

    for (uint8_t i = 0; i < RX_LENGTH; i++) {
        decoded[i] = reverseBits(raw[i]);
    }

    // External CRC covers length byte + 9-byte V2 payload.
    uint16_t calculatedCRC = calcCRC(
        decoded,
        RX_LENGTH - 2
    );

    uint16_t receivedCRC =
        ((uint16_t)decoded[RX_LENGTH - 1] << 8) |
        decoded[RX_LENGTH - 2];

    bool validLength = (decoded[0] == 9);
    bool validCRC = (calculatedCRC == receivedCRC);

    Serial.println();
    Serial.println("================================");

    Serial.print("PACKET #");
    Serial.println(++packetCounter);

    Serial.print("LABEL: ");
    Serial.println(actionLabel);

    Serial.print("CHANNEL: ");
    Serial.println(NRF_CHANNEL);

    Serial.print("RAW: ");

    for (uint8_t i = 0; i < RX_LENGTH; i++) {
        printHex(raw[i]);
        Serial.print(" ");
    }

    Serial.print("\nDECODED: ");

    for (uint8_t i = 0; i < RX_LENGTH; i++) {
        printHex(decoded[i]);
        Serial.print(" ");
    }

    Serial.print("\nLENGTH PREFIX: 0x");
    printHex(decoded[0]);

    Serial.print("\nLENGTH STATUS: ");
    Serial.println(validLength ? "VALID" : "UNEXPECTED");

    Serial.print("PAYLOAD: ");

    for (uint8_t i = 1; i <= 9; i++) {
        printHex(decoded[i]);
        Serial.print(" ");
    }

    Serial.print("\nCRC CALCULATED: 0x");
    Serial.println(calculatedCRC, HEX);

    Serial.print("CRC RECEIVED:   0x");
    Serial.println(receivedCRC, HEX);

    Serial.print("CRC STATUS: ");
    Serial.println(validCRC ? "VALID" : "INVALID");

    Serial.println("================================");

    // Decode V2 only when external frame is valid.
    if (validLength && validCRC) {

        uint8_t v2Packet[9];

        memcpy(v2Packet, &decoded[1], 9);

        decodeV2Packet(v2Packet);

        printV2Packet(v2Packet);
    } else {
        Serial.println("V2 DECODE SKIPPED: INVALID RADIO FRAME");
    }
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Local protocol/command self-test. No RF transmission is performed.
    runRemoteProtocolSelfTest();

    SPI.begin(
        NRF_SCK,
        NRF_MISO,
        NRF_MOSI,
        NRF_CSN
    );

    Serial.println();
    Serial.println("=== RC03RFB RGBCCT RADIO SCANNER ===");

    if (!radio.begin()) {
        Serial.println("ERROR: NRF24 NOT DETECTED!");

        while (true) {
            delay(1000);
        }
    }

    Serial.println("NRF24 OK");

    radio.setPALevel(RF24_PA_MIN);
    radio.setDataRate(RF24_1MBPS);
    radio.setAutoAck(false);
    radio.disableCRC();
    radio.setAddressWidth(5);

    radio.openReadingPipe(1, address);
    radio.setPayloadSize(RX_LENGTH);
    radio.setChannel(NRF_CHANNEL);

    radio.flush_rx();
    radio.startListening();

    // Configure/read back the PTX profile on all three channels, then restore RX.
    // This is a dry run only: no RF24::write() or writeFast() call is made.
    runRemoteRadioHardwareSelfTest();

    Serial.println("PROFILE: RGBCCT");
    Serial.println("LOGICAL CHANNEL: 8");
    Serial.println("NRF24 CHANNEL: 10");

    Serial.print("ADDRESS: ");

    for (uint8_t i = 0; i < 5; i++) {
        printHex(address[i]);
        Serial.print(" ");
    }

    Serial.println();
    Serial.println("RX LENGTH: 12");

    printMenu();
}

// --------------------------------------------------
// LOOP
// --------------------------------------------------

void loop() {
    processSerial();

    while (radio.available()) {
        uint8_t raw[RX_LENGTH] = {};

        radio.read(raw, RX_LENGTH);

        if (actionLabel[0] != '\0') {
            printPacket(raw);
        }
    }
}
