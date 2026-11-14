
#include "RemoteManager.h"
#include <string.h>
#include "RemoteProtocol.h"
#include "RemoteRadio.h"
#include "../LED/LedController.h"
#include "../STORAGE/EepromManager.h"
#include "../RTC/RtcManager.h"
#include "../config.h"

extern LedController led;
extern EepromManager storage;
extern bool saveCurrentState();

namespace REMOTE {
namespace {
constexpr uint8_t NRF_CE=15, NRF_CSN=7, NRF_SCK=4, NRF_MISO=6, NRF_MOSI=5;
constexpr uint8_t NRF_CHANNEL=10, RX_LENGTH=12;
constexpr uint32_t SPEED_REPEAT_TIMEOUT_MS=600;
constexpr uint32_t CONTROL_REPEAT_TIMEOUT_MS=600;

RemoteAction lastSpeedAction=RemoteAction::SPEED_MINUS;
uint32_t lastSpeedPacketMillis=0;
bool speedActionSeen=false;

RemoteAction lastControlAction=RemoteAction::SUN_TOP;
uint32_t lastControlPacketMillis=0;
bool controlActionSeen=false;

static const RemoteAction ACTIONS[]={
    RemoteAction::ON, RemoteAction::OFF, RemoteAction::SUN_TOP,
    RemoteAction::MODE, RemoteAction::O_MINUS, RemoteAction::WHITE,
    RemoteAction::O_PLUS, RemoteAction::SPEED_MINUS, RemoteAction::SUN_BOTTOM,
    RemoteAction::SPEED_PLUS, RemoteAction::GROUP_1_TOP,
    RemoteAction::GROUP_1_BOTTOM, RemoteAction::GROUP_2_TOP,
    RemoteAction::GROUP_2_BOTTOM, RemoteAction::GROUP_3_TOP,
    RemoteAction::GROUP_3_BOTTOM, RemoteAction::GROUP_4_TOP,
    RemoteAction::GROUP_4_BOTTOM
};

bool isSpeedAction(RemoteAction a) {
    return a==RemoteAction::SPEED_PLUS || a==RemoteAction::SPEED_MINUS;
}

bool isGroupAction(RemoteAction a) {
    return a==RemoteAction::GROUP_1_TOP ||
           a==RemoteAction::GROUP_1_BOTTOM ||
           a==RemoteAction::GROUP_2_TOP ||
           a==RemoteAction::GROUP_2_BOTTOM ||
           a==RemoteAction::GROUP_3_TOP ||
           a==RemoteAction::GROUP_3_BOTTOM ||
           a==RemoteAction::GROUP_4_TOP ||
           a==RemoteAction::GROUP_4_BOTTOM;
}

bool shouldHandleSpeedAction(RemoteAction a,uint8_t command) {
    if(!isSpeedAction(a)) return true;

    const uint32_t now=millis();

    if(command&0x80) {
        lastSpeedAction=a;
        lastSpeedPacketMillis=now;
        speedActionSeen=true;
        Serial.println("[REMOTE] SPEED ACTION IGNORED - REPEAT FLAG");
        return false;
    }

    if(speedActionSeen && a==lastSpeedAction &&
       now-lastSpeedPacketMillis<SPEED_REPEAT_TIMEOUT_MS) {
        lastSpeedPacketMillis=now;
        Serial.println("[REMOTE] SPEED ACTION IGNORED - DUPLICATE");
        return false;
    }

    lastSpeedAction=a;
    lastSpeedPacketMillis=now;
    speedActionSeen=true;
    return true;
}

bool shouldHandleControlAction(RemoteAction a) {
    const uint32_t now=millis();

    if(controlActionSeen && a==lastControlAction &&
       now-lastControlPacketMillis<CONTROL_REPEAT_TIMEOUT_MS) {
        lastControlPacketMillis=now;
        Serial.println("[REMOTE] CONTROL ACTION IGNORED - REPEAT");
        return false;
    }

    lastControlAction=a;
    lastControlPacketMillis=now;
    controlActionSeen=true;
    return true;
}

uint8_t reverseBits(uint8_t v) {
    v=(v&0xF0)>>4|(v&0x0F)<<4;
    v=(v&0xCC)>>2|(v&0x33)<<2;
    v=(v&0xAA)>>1|(v&0x55)<<1;
    return v;
}

uint16_t calculateRadioCrc(const uint8_t* data,uint8_t length) {
    uint16_t state=0;

    for(uint8_t i=0;i<length;i++) {
        uint8_t value=data[i];

        for(uint8_t bit=0;bit<8;bit++) {
            if((value^state)&0x01) state=(state>>1)^0x8408;
            else state>>=1;
            value>>=1;
        }
    }

    return state;
}

void restoreLastActiveMode() {
    uint8_t savedMode=0xFF;

    if(!storage.loadLastActiveLedMode(savedMode)) {
        Serial.println("[REMOTE] Brak ostatniego trybu. Ustawiam STATIC WHITE.");
        led.setStaticWhite();
        storage.saveLastActiveLedMode((uint8_t)LedMode::STATIC_WHITE);
        return;
    }

    switch((LedMode)savedMode) {
        case LedMode::FLOW:
            Serial.println("[REMOTE] ON -> RESTORE FLOW");
            led.startFlow();
            break;

        case LedMode::STATIC_WHITE:
            Serial.println("[REMOTE] ON -> RESTORE STATIC WHITE");
            led.setStaticWhite();
            break;

        case LedMode::BREATHING:
            Serial.println("[REMOTE] ON -> RESTORE BREATHING");
            led.startBreathing();
            break;

        default:
            Serial.println("[REMOTE] Niepoprawny tryb. Ustawiam STATIC WHITE.");
            led.setStaticWhite();
            storage.saveLastActiveLedMode((uint8_t)LedMode::STATIC_WHITE);
            break;
    }
}
} // namespace

RemoteManager::RemoteManager():radio(NRF_CE,NRF_CSN),packetCounter(0) {}

bool RemoteManager::begin() {
    Serial.println();
    Serial.println("========================================");
    Serial.println(" RC03RFB REMOTE MANAGER - RX ONLY");
    Serial.println("========================================");

    SPI.begin(NRF_SCK,NRF_MISO,NRF_MOSI,NRF_CSN);

    if(!radio.begin()) {
        Serial.println("[REMOTE] ERROR: NRF24 NOT DETECTED!");
        return false;
    }

    radio.setPALevel(RF24_PA_MIN);
    radio.setDataRate(RF24_1MBPS);
    radio.setAutoAck(false);
    radio.disableCRC();
    radio.setAddressWidth(RADIO_ADDRESS_SIZE);
    radio.openReadingPipe(1,RADIO_ADDRESS);
    radio.setPayloadSize(RX_LENGTH);
    radio.setChannel(NRF_CHANNEL);
    radio.flush_rx();
    radio.startListening();

    Serial.println("[REMOTE] NRF24 DETECTED");
    Serial.println("[REMOTE] RX MODE ACTIVE");
    Serial.println("[REMOTE] AUTO ACK: DISABLED");
    Serial.println("[REMOTE] HARDWARE CRC: DISABLED");
    Serial.printf("[REMOTE] CHANNEL: %u\n",NRF_CHANNEL);
    Serial.println("[REMOTE] RECEIVER READY");

    return true;
}

void RemoteManager::update() {
    while(radio.available()) {
        uint8_t raw[RX_LENGTH]={};
        radio.read(raw,RX_LENGTH);
        processPacket(raw);
    }
}

bool RemoteManager::decodePacket(const uint8_t* raw,uint8_t& command,uint8_t& argument,uint8_t& group) {
    uint8_t logical[RX_LENGTH]={};

    for(uint8_t i=0;i<RX_LENGTH;i++)
        logical[i]=reverseBits(raw[i]);

    if(logical[0]!=9) return false;

    const uint16_t calculated=calculateRadioCrc(logical,10);
    const uint16_t received=(uint16_t)logical[10]|((uint16_t)logical[11]<<8);

    if(calculated!=received) return false;

    uint8_t decoded[V2_PACKET_SIZE]={};
    if(!decodeAndValidateV2Packet(&logical[1],decoded)) return false;

    const uint16_t deviceId=((uint16_t)decoded[2]<<8)|decoded[3];

    if(decoded[0]!=0xEE || decoded[1]!=0x25 || deviceId!=0x275D)
        return false;

    command=decoded[4];
    argument=decoded[5];
    group=decoded[7];

    return true;
}

bool RemoteManager::identifyAction(uint8_t command,uint8_t argument,uint8_t group,RemoteAction& action) {
    const uint8_t base=command&0x7F;
    if(base==0x02) return false;

    for(uint8_t i=0;i<sizeof(ACTIONS)/sizeof(ACTIONS[0]);i++) {
        const RemoteAction candidate=ACTIONS[i];
        const uint8_t count=getCommandVariantCount(candidate);

        for(uint8_t v=0;v<count;v++) {
            RemoteCommand variant={};
            if(!getCommandVariant(candidate,v,&variant)) continue;

            const bool commandMatches=(variant.command&0x7F)==base;
            const bool argumentMatches=variant.argument==argument;
            const bool groupMatches=!isGroupAction(candidate) || variant.group==group;

            if(commandMatches && argumentMatches && groupMatches) {
                action=candidate;
                return true;
            }
        }
    }

    return false;
}

bool RemoteManager::processPacket(const uint8_t* raw) {
    uint8_t command=0,argument=0,group=0;

    if(!decodePacket(raw,command,argument,group)) return false;

    packetCounter++;

    Serial.printf("[REMOTE] PACKET #%lu | CMD=0x%02X ARG=0x%02X GROUP=0x%02X\n",
                  (unsigned long)packetCounter,command,argument,group);

    // RC03RFB color wheel: CMD 0x02, ARG carries 8-bit Hue.
    if((command&0x7F)==0x02) {
        led.setColorFromHue(argument);
        return true;
    }

    RemoteAction action;

    // SUN_TOP / SUN_BOTTOM: CMD 0x05.
    // Z logów: SUN_TOP rozpoczyna się zwykle od niższych ARG,
    // SUN_BOTTOM od wyższych ARG.
    if((command&0x7F)==0x05) {
        action=(argument<=0x50) ? RemoteAction::SUN_TOP : RemoteAction::SUN_BOTTOM;

        Serial.printf("[REMOTE] BRIGHTNESS CONTROL: %s\n",
                      action==RemoteAction::SUN_TOP ? "SUN_TOP" : "SUN_BOTTOM");

        if(!shouldHandleControlAction(action)) return true;

        handleAction(action);
        return true;
    }

    // O_PLUS / O_MINUS: CMD 0x07.
    // Z przechwyconych logów O_MINUS ma ARG=0x64,
    // natomiast O_PLUS wysyła zmieniające się wartości ARG.
    if((command&0x7F)==0x07) {
        action=(argument==0x64) ? RemoteAction::O_MINUS : RemoteAction::O_PLUS;

        Serial.printf("[REMOTE] SATURATION CONTROL: %s\n",
                      action==RemoteAction::O_PLUS ? "O_PLUS" : "O_MINUS");

        if(!shouldHandleControlAction(action)) return true;

        handleAction(action);
        return true;
    }

    if(!identifyAction(command,argument,group,action)) {
        Serial.println("[REMOTE] UNKNOWN COMMAND");
        return false;
    }

    Serial.printf("[REMOTE] ACTION: %s\n",getActionName(action));

    if(!shouldHandleSpeedAction(action,command)) return true;

    handleAction(action);
    return true;
}

void RemoteManager::handleAction(RemoteAction action) {
    const LedMode previousMode=led.getMode();

    switch(action) {
        case RemoteAction::ON:
            Serial.println("[REMOTE] ON");
            restoreLastActiveMode();
            break;

        case RemoteAction::WHITE:
            Serial.println("[REMOTE] WHITE");
            led.setStaticWhite();
            storage.saveLastActiveLedMode((uint8_t)LedMode::STATIC_WHITE);
            break;

        case RemoteAction::OFF:
            Serial.println("[REMOTE] OFF");
            if(led.isOn()) led.turnOff();
            else led.resetEffectSpeed();
            break;

        case RemoteAction::SUN_TOP:
            Serial.println("[REMOTE] SUN TOP -> BRIGHTNESS UP");
            led.increaseBrightness();
            break;

        case RemoteAction::SUN_BOTTOM:
            Serial.println("[REMOTE] SUN BOTTOM -> BRIGHTNESS DOWN");
            led.decreaseBrightness();
            break;

        case RemoteAction::O_PLUS:
            Serial.println("[REMOTE] O PLUS -> SATURATION UP");
            led.increaseSaturation();
            break;

        case RemoteAction::O_MINUS:
            Serial.println("[REMOTE] O MINUS -> SATURATION DOWN");
            led.decreaseSaturation();
            break;

        case RemoteAction::SPEED_PLUS:
            Serial.println("[REMOTE] SPEED PLUS");
            led.increaseEffectSpeed();
            break;

        case RemoteAction::SPEED_MINUS:
            Serial.println("[REMOTE] SPEED MINUS");
            led.decreaseEffectSpeed();
            break;

        case RemoteAction::GROUP_1_TOP:
            Serial.println("[REMOTE] GROUP 1 TOP -> FLOW");
            led.startFlow();
            storage.saveLastActiveLedMode((uint8_t)LedMode::FLOW);
            break;

        case RemoteAction::GROUP_2_TOP:
            Serial.println("[REMOTE] GROUP 2 TOP -> BREATHING WHITE");
            led.startBreathing(0,0,0,255);
            storage.saveLastActiveLedMode((uint8_t)LedMode::BREATHING);
            break;

        default:
            Serial.printf("[REMOTE] ACTION %s - NOT IMPLEMENTED\n",getActionName(action));
            break;
    }

    const LedMode currentMode=led.getMode();

    if(currentMode!=previousMode) {
        Serial.printf("[REMOTE] MODE CHANGE: 0x%02X -> 0x%02X\n",
                      (uint8_t)previousMode,(uint8_t)currentMode);

        if(!saveCurrentState())
            Serial.println("[REMOTE] ERROR: nie udalo sie zapisac aktualnego stanu LED.");
    }
}
} // namespace REMOTE
