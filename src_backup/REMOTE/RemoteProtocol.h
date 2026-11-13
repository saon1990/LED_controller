#pragma once

#include <stdint.h>

namespace REMOTE {

// MiLight V2 packets contain 9 bytes before the external radio CRC.
static constexpr uint8_t V2_PACKET_SIZE = 9;

// Encode a decoded MiLight V2 packet into its on-air V2 representation.
// Input and output may be separate buffers; in-place operation is supported.
void encodeV2Packet(const uint8_t decoded[V2_PACKET_SIZE],
                    uint8_t encoded[V2_PACKET_SIZE]);

// Decode an encoded MiLight V2 packet into its decoded representation.
// Input and output may be separate buffers; in-place operation is supported.
void decodeV2Packet(const uint8_t encoded[V2_PACKET_SIZE],
                    uint8_t decoded[V2_PACKET_SIZE]);

// Validate byte 8 against the V2 checksum rule, using a decoded packet.
bool validateV2Checksum(const uint8_t decoded[V2_PACKET_SIZE]);

// Convenience helper: decode an encoded packet and validate its checksum.
bool decodeAndValidateV2Packet(
    const uint8_t encoded[V2_PACKET_SIZE],
    uint8_t decoded[V2_PACKET_SIZE]);

// Exposed for diagnostics and unit tests.
uint8_t calculateXorKey(uint8_t packetKey);

} // namespace REMOTE
