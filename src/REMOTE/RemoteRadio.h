#pragma once

#include <stdint.h>
#include <RF24.h>
#include "RemoteCommands.h"

namespace REMOTE {

// FUT089 / RGB+CCT radio profile used by the RC03RFB capture.
static constexpr uint8_t RADIO_ADDRESS_SIZE = 5;
static constexpr uint8_t RADIO_PAYLOAD_SIZE = 12;
static constexpr uint8_t RADIO_CHANNEL_COUNT = 3;
static constexpr uint8_t RADIO_LOGICAL_CHANNELS[RADIO_CHANNEL_COUNT] = {
    8, 39, 70
};
static constexpr uint8_t RADIO_NRF_CHANNELS[RADIO_CHANNEL_COUNT] = {
    10, 41, 72
};
static constexpr uint8_t RADIO_ADDRESS[RADIO_ADDRESS_SIZE] = {
    0x8A, 0x01, 0xE9, 0xC4, 0x56
};

// `payload` is already bit-reversed byte-by-byte, ready to be supplied to
// RF24::write() in a future, explicitly enabled TX stage.
// This module only prepares data; it never accesses the radio or transmits.
struct RadioFrame {
    uint8_t address[RADIO_ADDRESS_SIZE];
    uint8_t channel;
    uint8_t payload[RADIO_PAYLOAD_SIZE];
};

// Build a complete 12-byte nRF24 payload for one captured command variant.
// channelIndex: 0 -> logical 8 / NRF 10; 1 -> 39 / 41; 2 -> 70 / 72.
// sequence is supplied by the caller; no sequence policy is assumed here.
bool buildRadioFrame(const RemoteCommand& command,
                     uint8_t sequence,
                     uint8_t channelIndex,
                     RadioFrame* outFrame);

// Validate a prepared frame by reversing its bytes, checking length and
// external CRC16, then decoding and checking the embedded MiLight V2 packet.
bool validateRadioFrame(const RadioFrame& frame);

// Configure the nRF24 in PTX mode for the selected MiLight channel.
// This function NEVER sends a packet: it does not call write()/writeFast()
// and keeps CE low through RF24's standby/PTX configuration path.
bool configureTransmitProfile(RF24& radio, uint8_t nrfChannel);

// Restore the existing scanner RX profile after a TX-configuration dry run.
void restoreReceiveProfile(RF24& radio,
                           uint8_t nrfChannel,
                           const uint8_t address[RADIO_ADDRESS_SIZE],
                           uint8_t payloadSize);

// Exposed for deterministic tests and diagnostics.
uint16_t calculateRadioCrc16(const uint8_t* data, uint8_t length);
uint8_t reverseRadioBits(uint8_t value);

} // namespace REMOTE
