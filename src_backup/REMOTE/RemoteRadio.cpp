#include "RemoteRadio.h"

#include <string.h>

#include "RemoteProtocol.h"

namespace REMOTE {
namespace {

static constexpr uint8_t V2_KEY = 0xEE;
static constexpr uint8_t V2_PROTOCOL_ID = 0x25;
static constexpr uint16_t V2_DEVICE_ID = 0x275D;
static constexpr uint8_t V2_LENGTH_PREFIX = 9;

uint8_t calculateDecodedV2Checksum(const uint8_t packet[V2_PACKET_SIZE]) {
    uint8_t sum = calculateXorKey(packet[0]);
    for (uint8_t i = 1; i <= 7; ++i) {
        sum = static_cast<uint8_t>(sum + packet[i]);
    }
    return static_cast<uint8_t>(sum + 2);
}

} // namespace

uint8_t reverseRadioBits(uint8_t value) {
    value = static_cast<uint8_t>((value & 0xF0) >> 4 | (value & 0x0F) << 4);
    value = static_cast<uint8_t>((value & 0xCC) >> 2 | (value & 0x33) << 2);
    value = static_cast<uint8_t>((value & 0xAA) >> 1 | (value & 0x55) << 1);
    return value;
}

uint16_t calculateRadioCrc16(const uint8_t* data, uint8_t length) {
    if (data == nullptr) {
        return 0;
    }

    uint16_t state = 0;
    for (uint8_t i = 0; i < length; ++i) {
        uint8_t value = data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if ((value ^ state) & 0x01) {
                state = static_cast<uint16_t>((state >> 1) ^ 0x8408);
            } else {
                state = static_cast<uint16_t>(state >> 1);
            }
            value >>= 1;
        }
    }
    return state;
}

bool buildRadioFrame(const RemoteCommand& command,
                     uint8_t sequence,
                     uint8_t channelIndex,
                     RadioFrame* outFrame) {
    if (outFrame == nullptr || channelIndex >= RADIO_CHANNEL_COUNT) {
        return false;
    }

    uint8_t decodedV2[V2_PACKET_SIZE] = {
        V2_KEY,
        V2_PROTOCOL_ID,
        static_cast<uint8_t>((V2_DEVICE_ID >> 8) & 0xFF),
        static_cast<uint8_t>(V2_DEVICE_ID & 0xFF),
        command.command,
        command.argument,
        sequence,
        command.group,
        0
    };
    decodedV2[8] = calculateDecodedV2Checksum(decodedV2);

    uint8_t encodedV2[V2_PACKET_SIZE] = {};
    encodeV2Packet(decodedV2, encodedV2);

    // PL1167 payload bytes before the nRF24 bit-order adaptation:
    // length prefix + encoded V2 packet + little-endian external CRC16.
    uint8_t logicalPayload[RADIO_PAYLOAD_SIZE] = {};
    logicalPayload[0] = V2_LENGTH_PREFIX;
    memcpy(&logicalPayload[1], encodedV2, V2_PACKET_SIZE);

    const uint16_t crc = calculateRadioCrc16(logicalPayload, 10);
    logicalPayload[10] = static_cast<uint8_t>(crc & 0xFF);
    logicalPayload[11] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    memcpy(outFrame->address, RADIO_ADDRESS, RADIO_ADDRESS_SIZE);
    outFrame->channel = RADIO_NRF_CHANNELS[channelIndex];

    for (uint8_t i = 0; i < RADIO_PAYLOAD_SIZE; ++i) {
        outFrame->payload[i] = reverseRadioBits(logicalPayload[i]);
    }

    return true;
}

bool validateRadioFrame(const RadioFrame& frame) {
    bool validChannel = false;
    for (uint8_t i = 0; i < RADIO_CHANNEL_COUNT; ++i) {
        if (frame.channel == RADIO_NRF_CHANNELS[i]) {
            validChannel = true;
            break;
        }
    }
    if (!validChannel || memcmp(frame.address, RADIO_ADDRESS, RADIO_ADDRESS_SIZE) != 0) {
        return false;
    }

    uint8_t logicalPayload[RADIO_PAYLOAD_SIZE] = {};
    for (uint8_t i = 0; i < RADIO_PAYLOAD_SIZE; ++i) {
        logicalPayload[i] = reverseRadioBits(frame.payload[i]);
    }

    if (logicalPayload[0] != V2_LENGTH_PREFIX) {
        return false;
    }

    const uint16_t calculatedCrc = calculateRadioCrc16(logicalPayload, 10);
    const uint16_t receivedCrc = static_cast<uint16_t>(logicalPayload[10]) |
        static_cast<uint16_t>(logicalPayload[11] << 8);
    if (calculatedCrc != receivedCrc) {
        return false;
    }

    uint8_t decodedV2[V2_PACKET_SIZE] = {};
    decodeV2Packet(&logicalPayload[1], decodedV2);

    if (!validateV2Checksum(decodedV2)) {
        return false;
    }

    return decodedV2[0] == V2_KEY &&
           decodedV2[1] == V2_PROTOCOL_ID &&
           decodedV2[2] == static_cast<uint8_t>((V2_DEVICE_ID >> 8) & 0xFF) &&
           decodedV2[3] == static_cast<uint8_t>(V2_DEVICE_ID & 0xFF);
}

bool configureTransmitProfile(RF24& radio, uint8_t nrfChannel) {
    bool supportedChannel = false;
    for (uint8_t i = 0; i < RADIO_CHANNEL_COUNT; ++i) {
        if (nrfChannel == RADIO_NRF_CHANNELS[i]) {
            supportedChannel = true;
            break;
        }
    }
    if (!supportedChannel) {
        return false;
    }

    // PTX preparation only. No payload is queued and no write call is made.
    radio.stopListening();
    radio.setChannel(nrfChannel);
    radio.setDataRate(RF24_1MBPS);
    radio.setPALevel(RF24_PA_MIN);
    radio.setAutoAck(false);
    radio.disableCRC();
    radio.setAddressWidth(RADIO_ADDRESS_SIZE);
    radio.setPayloadSize(RADIO_PAYLOAD_SIZE);
    radio.openWritingPipe(RADIO_ADDRESS);

    // Verify the settings that RF24 exposes through public readback methods.
    return radio.isChipConnected() &&
           radio.getChannel() == nrfChannel &&
           radio.getDataRate() == RF24_1MBPS &&
           radio.getPALevel() == RF24_PA_MIN &&
           radio.getCRCLength() == RF24_CRC_DISABLED;
}

void restoreReceiveProfile(RF24& radio,
                           uint8_t nrfChannel,
                           const uint8_t address[RADIO_ADDRESS_SIZE],
                           uint8_t payloadSize) {
    radio.stopListening();
    radio.setChannel(nrfChannel);
    radio.setDataRate(RF24_1MBPS);
    radio.setPALevel(RF24_PA_MIN);
    radio.setAutoAck(false);
    radio.disableCRC();
    radio.setAddressWidth(RADIO_ADDRESS_SIZE);
    radio.openReadingPipe(1, address);
    radio.setPayloadSize(payloadSize);
    radio.flush_rx();
    radio.startListening();
}

} // namespace REMOTE
