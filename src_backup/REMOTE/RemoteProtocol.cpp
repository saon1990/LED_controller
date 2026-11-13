#include "RemoteProtocol.h"

#include <string.h>

namespace REMOTE {
namespace {

static constexpr uint8_t V2_OFFSETS[8][4] = {
    {0x45, 0x1F, 0x14, 0x5C},
    {0x2B, 0xC9, 0xE3, 0x11},
    {0x6D, 0x5F, 0x8A, 0x2B},
    {0xAF, 0x03, 0x1D, 0xF3},
    {0x1A, 0xE2, 0xF0, 0xD1},
    {0x04, 0xD8, 0x71, 0x42},
    {0xAF, 0x04, 0xDD, 0x07},
    {0x61, 0x13, 0x38, 0x64}
};

// Mirrors V2_OFFSET(byte, key, jumpStart) from the reference implementation.
uint8_t v2Offset(uint8_t byteIndex, uint8_t key, uint8_t jumpStart) {
    uint8_t offset = V2_OFFSETS[byteIndex - 1][key % 4];

    if (jumpStart > 0 && key >= jumpStart && key < (uint8_t)(jumpStart + 0x80)) {
        offset = static_cast<uint8_t>(offset + 0x80);
    }

    return offset;
}

uint8_t encodeByte(uint8_t value, uint8_t s1, uint8_t key, uint8_t s2) {
    value = static_cast<uint8_t>(value + s1);
    value ^= key;
    value = static_cast<uint8_t>(value + s2);
    return value;
}

uint8_t decodeByte(uint8_t value, uint8_t s1, uint8_t key, uint8_t s2) {
    value = static_cast<uint8_t>(value - s2);
    value ^= key;
    value = static_cast<uint8_t>(value - s1);
    return value;
}

} // namespace

uint8_t calculateXorKey(uint8_t packetKey) {
    const uint8_t shift = ((packetKey & 0x0F) < 0x04) ? 0 : 1;
    const uint8_t x = static_cast<uint8_t>(
        (((packetKey & 0xF0) >> 4) + shift + 6) % 8);
    const uint8_t msn = static_cast<uint8_t>(
        (((4 + x) ^ 1) & 0x0F) << 4);
    const uint8_t lsn = static_cast<uint8_t>(
        (((packetKey & 0x0F) + 4) ^ 2) & 0x0F);

    return static_cast<uint8_t>(msn | lsn);
}

void encodeV2Packet(const uint8_t decoded[V2_PACKET_SIZE],
                    uint8_t encoded[V2_PACKET_SIZE]) {
    if (decoded == nullptr || encoded == nullptr) {
        return;
    }

    // Copy first so callers may safely encode in place.
    uint8_t source[V2_PACKET_SIZE];
    memcpy(source, decoded, V2_PACKET_SIZE);
    memcpy(encoded, source, V2_PACKET_SIZE);

    const uint8_t packetKey = source[0];
    const uint8_t xorKey = calculateXorKey(packetKey);
    uint8_t sum = xorKey;

    for (uint8_t i = 1; i <= 7; ++i) {
        sum = static_cast<uint8_t>(sum + source[i]);
        encoded[i] = encodeByte(
            source[i],
            0,
            xorKey,
            v2Offset(i, packetKey, 0x54));
    }

    // The checksum byte uses s1=2 and the reference encoder's jumpStart=0.
    encoded[8] = encodeByte(
        sum,
        2,
        xorKey,
        v2Offset(8, packetKey, 0));
}

void decodeV2Packet(const uint8_t encoded[V2_PACKET_SIZE],
                    uint8_t decoded[V2_PACKET_SIZE]) {
    if (encoded == nullptr || decoded == nullptr) {
        return;
    }

    uint8_t source[V2_PACKET_SIZE];
    memcpy(source, encoded, V2_PACKET_SIZE);
    memcpy(decoded, source, V2_PACKET_SIZE);

    const uint8_t packetKey = source[0];
    const uint8_t xorKey = calculateXorKey(packetKey);

    for (uint8_t i = 1; i <= 8; ++i) {
        decoded[i] = decodeByte(
            source[i],
            0,
            xorKey,
            v2Offset(i, packetKey, 0x54));
    }
}

bool validateV2Checksum(const uint8_t decoded[V2_PACKET_SIZE]) {
    if (decoded == nullptr) {
        return false;
    }

    uint8_t sum = calculateXorKey(decoded[0]);

    for (uint8_t i = 1; i <= 7; ++i) {
        sum = static_cast<uint8_t>(sum + decoded[i]);
    }

    // The V2 encoder applies s1=2 to the checksum byte.
    const uint8_t expected = static_cast<uint8_t>(sum + 2);
    return decoded[8] == expected;
}

bool decodeAndValidateV2Packet(
    const uint8_t encoded[V2_PACKET_SIZE],
    uint8_t decoded[V2_PACKET_SIZE]) {
    decodeV2Packet(encoded, decoded);
    return validateV2Checksum(decoded);
}

} // namespace REMOTE
