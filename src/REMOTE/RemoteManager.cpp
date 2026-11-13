#include "RemoteManager.h"

#include <string.h>

#include "RemoteProtocol.h"
#include "RemoteRadio.h"

#include "../LED/LedController.h"
#include "../STORAGE/EepromManager.h"
#include "../RTC/RtcManager.h"
#include "../config.h"

// Obiekty są zdefiniowane w main.cpp.
extern LedController led;
extern EepromManager storage;

namespace REMOTE {

namespace {

constexpr uint8_t NRF_CE   = 15;
constexpr uint8_t NRF_CSN  = 7;
constexpr uint8_t NRF_SCK  = 4;
constexpr uint8_t NRF_MISO = 6;
constexpr uint8_t NRF_MOSI = 5;

constexpr uint8_t NRF_CHANNEL = 10;
constexpr uint8_t RX_LENGTH = 12;

static const RemoteAction ACTIONS[] = {
    RemoteAction::ON,
    RemoteAction::OFF,
    RemoteAction::SUN_TOP,
    RemoteAction::MODE,
    RemoteAction::O_MINUS,
    RemoteAction::WHITE,
    RemoteAction::O_PLUS,
    RemoteAction::SPEED_MINUS,
    RemoteAction::SUN_BOTTOM,
    RemoteAction::SPEED_PLUS,

    RemoteAction::GROUP_1_TOP,
    RemoteAction::GROUP_1_BOTTOM,
    RemoteAction::GROUP_2_TOP,
    RemoteAction::GROUP_2_BOTTOM,
    RemoteAction::GROUP_3_TOP,
    RemoteAction::GROUP_3_BOTTOM,
    RemoteAction::GROUP_4_TOP,
    RemoteAction::GROUP_4_BOTTOM
};

uint8_t reverseBits(uint8_t value)
{
    value = (value & 0xF0) >> 4 | (value & 0x0F) << 4;
    value = (value & 0xCC) >> 2 | (value & 0x33) << 2;
    value = (value & 0xAA) >> 1 | (value & 0x55) << 1;

    return value;
}

uint16_t calculateRadioCrc(
    const uint8_t* data,
    uint8_t length)
{
    uint16_t state = 0;

    for (uint8_t i = 0; i < length; i++)
    {
        uint8_t value = data[i];

        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if ((value ^ state) & 0x01)
                state = (state >> 1) ^ 0x8408;
            else
                state >>= 1;

            value >>= 1;
        }
    }

    return state;
}

void printHex(uint8_t value)
{
    if (value < 0x10)
        Serial.print('0');

    Serial.print(value, HEX);
}

void restoreLastActiveMode()
{
    uint8_t savedMode = 0xFF;

    if (!storage.loadLastActiveLedMode(savedMode))
    {
        Serial.println(
            "[REMOTE] Brak ostatniego trybu. Ustawiam STATIC WHITE."
        );

        led.setStaticWhite();
        storage.saveLastActiveLedMode(
            static_cast<uint8_t>(LedMode::STATIC_WHITE)
        );

        return;
    }

    switch (static_cast<LedMode>(savedMode))
    {
        case LedMode::FLOW:

            Serial.println(
                "[REMOTE] ON -> RESTORE FLOW"
            );

            led.startFlow();

            break;

        case LedMode::STATIC_WHITE:

            Serial.println(
                "[REMOTE] ON -> RESTORE STATIC WHITE"
            );

            led.setStaticWhite();

            break;

        default:

            Serial.println(
                "[REMOTE] Niepoprawny tryb. Ustawiam STATIC WHITE."
            );

            led.setStaticWhite();

            storage.saveLastActiveLedMode(
                static_cast<uint8_t>(LedMode::STATIC_WHITE)
            );

            break;
    }

    // Aktualny stan i timestamp zapisuje główna aplikacja.
}

} // namespace

RemoteManager::RemoteManager()
    : radio(NRF_CE, NRF_CSN),
      packetCounter(0)
{
}

bool RemoteManager::begin()
{
    Serial.println();
    Serial.println("========================================");
    Serial.println(" RC03RFB REMOTE MANAGER - RX ONLY");
    Serial.println("========================================");

    SPI.begin(
        NRF_SCK,
        NRF_MISO,
        NRF_MOSI,
        NRF_CSN
    );

    if (!radio.begin())
    {
        Serial.println(
            "[REMOTE] ERROR: NRF24 NOT DETECTED!"
        );

        return false;
    }

    radio.setPALevel(RF24_PA_MIN);
    radio.setDataRate(RF24_1MBPS);

    radio.setAutoAck(false);
    radio.disableCRC();

    radio.setAddressWidth(RADIO_ADDRESS_SIZE);

    radio.openReadingPipe(
        1,
        RADIO_ADDRESS
    );

    radio.setPayloadSize(RX_LENGTH);
    radio.setChannel(NRF_CHANNEL);

    radio.flush_rx();
    radio.startListening();

    Serial.println("[REMOTE] NRF24 DETECTED");
    Serial.println("[REMOTE] RX MODE ACTIVE");
    Serial.println("[REMOTE] AUTO ACK: DISABLED");
    Serial.println("[REMOTE] HARDWARE CRC: DISABLED");
    Serial.printf(
        "[REMOTE] CHANNEL: %u\n",
        NRF_CHANNEL
    );

    Serial.println("[REMOTE] RECEIVER READY");

    return true;
}

void RemoteManager::update()
{
    while (radio.available())
    {
        uint8_t raw[RX_LENGTH] = {};

        radio.read(raw, RX_LENGTH);

        processPacket(raw);
    }
}

bool RemoteManager::decodePacket(
    const uint8_t* raw,
    uint8_t& command,
    uint8_t& argument,
    uint8_t& group)
{
    uint8_t logical[RX_LENGTH] = {};

    for (uint8_t i = 0; i < RX_LENGTH; i++)
        logical[i] = reverseBits(raw[i]);

    if (logical[0] != 9)
        return false;

    const uint16_t calculatedCrc =
        calculateRadioCrc(logical, 10);

    const uint16_t receivedCrc =
        static_cast<uint16_t>(logical[10]) |
        (static_cast<uint16_t>(logical[11]) << 8);

    if (calculatedCrc != receivedCrc)
        return false;

    uint8_t decoded[V2_PACKET_SIZE] = {};

    if (!decodeAndValidateV2Packet(
            &logical[1],
            decoded))
    {
        return false;
    }

    const uint16_t deviceId =
        (static_cast<uint16_t>(decoded[2]) << 8) |
        decoded[3];

    if (
        decoded[0] != 0xEE ||
        decoded[1] != 0x25 ||
        deviceId != 0x275D
    )
    {
        return false;
    }

    command = decoded[4];
    argument = decoded[5];
    group = decoded[7];

    return true;
}

bool RemoteManager::identifyAction(
    uint8_t command,
    uint8_t argument,
    uint8_t group,
    RemoteAction& action)
{
    const uint8_t commandBase = command & 0x7F;

    // Koło kolorów i COLOR_CYCLE pomijamy w tej wersji.
    if (commandBase == 0x02)
        return false;

    for (uint8_t i = 0;
         i < sizeof(ACTIONS) / sizeof(ACTIONS[0]);
         i++)
    {
        const RemoteAction candidate = ACTIONS[i];

        const uint8_t count =
            getCommandVariantCount(candidate);

        for (uint8_t v = 0; v < count; v++)
        {
            RemoteCommand variant = {};

            if (!getCommandVariant(
                    candidate,
                    v,
                    &variant))
            {
                continue;
            }

            if (
                (variant.command & 0x7F) == commandBase &&
                variant.argument == argument &&
                variant.group == group
            )
            {
                action = candidate;
                return true;
            }
        }
    }

    return false;
}

bool RemoteManager::processPacket(const uint8_t* raw)
{
    uint8_t command = 0;
    uint8_t argument = 0;
    uint8_t group = 0;

    if (!decodePacket(
            raw,
            command,
            argument,
            group))
    {
        return false;
    }

    packetCounter++;

    Serial.printf(
        "[REMOTE] PACKET #%lu | CMD=0x%02X ARG=0x%02X GROUP=0x%02X\n",
        static_cast<unsigned long>(packetCounter),
        command,
        argument,
        group
    );

    RemoteAction action;

    if (!identifyAction(
            command,
            argument,
            group,
            action))
    {
        if ((command & 0x7F) == 0x02)
        {
            Serial.println(
                "[REMOTE] COLOR CONTROL - IGNORED"
            );
        }
        else
        {
            Serial.println(
                "[REMOTE] UNKNOWN COMMAND"
            );
        }

        return false;
    }

    Serial.printf(
        "[REMOTE] ACTION: %s\n",
        getActionName(action)
    );

    handleAction(action);

    return true;
}

void RemoteManager::handleAction(RemoteAction action)
{
    switch (action)
    {
        case RemoteAction::ON:

            Serial.println("[REMOTE] ON");

            restoreLastActiveMode();

            break;

        case RemoteAction::WHITE:

            Serial.println("[REMOTE] WHITE");

            led.setStaticWhite();

            storage.saveLastActiveLedMode(
                static_cast<uint8_t>(LedMode::STATIC_WHITE)
            );

            break;

        case RemoteAction::OFF:

            Serial.println("[REMOTE] OFF");

            // LedController zachowuje dotychczasową animację FLOW_OFF.
            // Nie zmieniamy ostatniego aktywnego trybu.

            if (led.isOn())
            {
                led.turnOff();
            }

            break;

        case RemoteAction::GROUP_1_TOP:

            Serial.println("[REMOTE] GROUP 1 TOP -> FLOW");

            led.startFlow();

            storage.saveLastActiveLedMode(
                static_cast<uint8_t>(LedMode::FLOW)
            );

            break;

        default:

            Serial.printf(
                "[REMOTE] ACTION %s - NOT IMPLEMENTED\n",
                getActionName(action)
            );

            break;
    }
}

} // namespace REMOTE