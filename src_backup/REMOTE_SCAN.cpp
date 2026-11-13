
#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <string.h>

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
    Serial.println("[1] WHEEL_CENTER");

    Serial.println();
    Serial.println("[0] PAUSE PACKET LOGGING");

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
        char key = Serial.read();

        // Ignore line endings
        if (key == '\r' || key == '\n') {
            continue;
        }

        // Convert lowercase to uppercase
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

            case '1': setLabel("WHEEL_CENTER"); break;

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
// CRC16
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
// PACKET PROCESSING
// --------------------------------------------------

void printPacket(uint8_t* raw) {
    uint8_t decoded[RX_LENGTH] = {};

    for (uint8_t i = 0; i < RX_LENGTH; i++) {
        decoded[i] = reverseBits(raw[i]);
    }

    // CRC covers length byte + packet data
    uint16_t calculatedCRC = calcCRC(
        decoded,
        RX_LENGTH - 2
    );

    // CRC transmitted low byte first
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
}


// --------------------------------------------------
// SETUP
// --------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(1000);

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

        // No active label = discard packet from log
        if (actionLabel[0] != '\0') {
            printPacket(raw);
        }
    }
}
