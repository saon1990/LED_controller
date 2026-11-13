#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>

#include "RemoteCommands.h"

namespace REMOTE {

class RemoteManager
{
public:
    RemoteManager();

    bool begin();
    void update();

private:
    RF24 radio;

    bool processPacket(const uint8_t* raw);
    bool decodePacket(const uint8_t* raw,
                      uint8_t& command,
                      uint8_t& argument,
                      uint8_t& group);

    bool identifyAction(uint8_t command,
                        uint8_t argument,
                        uint8_t group,
                        RemoteAction& action);

    void handleAction(RemoteAction action);

    uint32_t packetCounter;
};

}