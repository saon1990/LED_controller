#include "LedController.h"
#include "config.h"
#include <math.h>

LedController::LedController()
    : strip(LED_COUNT, LED_DATA_PIN, NEO_GRBW + NEO_KHZ800),
      mode(LedMode::OFF), animation(LedAnimation::NONE),
      animationFront(0.0f), lastFrameMillis(0),
      effectSpeed(DEFAULT_EFFECT_SPEED),
      currentR(0), currentG(0), currentB(0), currentW(255),
      breathingStartMillis(0), breathingCycleMs(BREATHING_CYCLE_MS),
      breathingR(0), breathingG(0), breathingB(0), breathingW(255) {}

void LedController::begin() {
    strip.begin(); strip.setBrightness(LED_BRIGHTNESS); clear();
}
bool LedController::isOn() const { return mode != LedMode::OFF; }
LedMode LedController::getMode() const { return mode; }

void LedController::resetEffectSpeed() {
    effectSpeed=DEFAULT_EFFECT_SPEED;
    Serial.printf("[LED] EFFECT SPEED RESET: %u\n",effectSpeed);
}
uint8_t LedController::getEffectSpeed() const { return effectSpeed; }

uint32_t LedController::getBreathingCycleForSpeed() const {
    static const uint32_t times[10]={12000,10000,8000,7000,6000,5000,4000,3000,2500,2000};
    return times[effectSpeed-1];
}
void LedController::updateBreathingSpeed() {
    if(animation!=LedAnimation::BREATHING || !breathingCycleMs) return;
    const unsigned long now=millis(), elapsed=now-breathingStartMillis;
    const float phase=float(elapsed%breathingCycleMs)/float(breathingCycleMs);
    const uint32_t next=getBreathingCycleForSpeed();
    breathingStartMillis=now-static_cast<unsigned long>(phase*next);
    breathingCycleMs=next;
    Serial.printf("[LED] BREATHING CYCLE: %lu ms\n",(unsigned long)breathingCycleMs);
}
float LedController::getFlowMoveSpeed() const {
    static const float speeds[10]={0.15f,0.20f,0.24f,0.30f,0.35f,0.42f,0.49f,0.56f,0.63f,0.70f};
    return speeds[effectSpeed-1];
}
void LedController::increaseEffectSpeed() {
    if(mode!=LedMode::FLOW && mode!=LedMode::BREATHING) { Serial.println("[LED] SPEED_PLUS - brak aktywnego efektu."); return; }
    if(effectSpeed>=MAX_EFFECT_SPEED) { Serial.printf("[LED] EFFECT SPEED: MAX (%u)\n",effectSpeed); return; }
    ++effectSpeed; updateBreathingSpeed();
    if(mode==LedMode::FLOW) Serial.printf("[LED] FLOW MOVE SPEED: %.2f\n",getFlowMoveSpeed());
    Serial.printf("[LED] EFFECT SPEED: %u/%u\n",effectSpeed,MAX_EFFECT_SPEED);
}
void LedController::decreaseEffectSpeed() {
    if(mode!=LedMode::FLOW && mode!=LedMode::BREATHING) { Serial.println("[LED] SPEED_MINUS - brak aktywnego efektu."); return; }
    if(effectSpeed<=MIN_EFFECT_SPEED) { Serial.printf("[LED] EFFECT SPEED: MIN (%u)\n",effectSpeed); return; }
    --effectSpeed; updateBreathingSpeed();
    if(mode==LedMode::FLOW) Serial.printf("[LED] FLOW MOVE SPEED: %.2f\n",getFlowMoveSpeed());
    Serial.printf("[LED] EFFECT SPEED: %u/%u\n",effectSpeed,MAX_EFFECT_SPEED);
}
void LedController::clear() {
    mode=LedMode::OFF; animation=LedAnimation::NONE; animationFront=0.0f;
    resetEffectSpeed(); strip.clear(); strip.show();
}
uint32_t LedController::makeColor(uint8_t r,uint8_t g,uint8_t b,uint8_t w) {
    // Adafruit NeoPixel dla NEO_GRBW oczekuje argumentow R,G,B,W.
    // Zachowujemy mapowanie używane dotychczas w projekcie.
    return strip.Color(r,w,g,b);
}
float LedController::smoothStep(float x) {
    if(x<=0.0f) return 0.0f; if(x>=1.0f) return 1.0f;
    return x*x*(3.0f-2.0f*x);
}
void LedController::setAllWhite() {
    for(uint16_t i=0;i<LED_COUNT;i++) strip.setPixelColor(i,makeColor(0,0,0,255));
    strip.show();
}
void LedController::setAllCurrentColor() {
    const uint32_t c=makeColor(currentR,currentG,currentB,currentW);
    for(uint16_t i=0;i<LED_COUNT;i++) strip.setPixelColor(i,c);
    strip.show();
}
void LedController::setStaticWhite() {
    mode=LedMode::STATIC_WHITE; animation=LedAnimation::NONE; animationFront=0;
    resetEffectSpeed(); setAllWhite(); Serial.println("[LED] MODE: STATIC WHITE");
}
void LedController::startFlow() {
    if(mode==LedMode::FLOW && animation!=LedAnimation::FLOW_OFF) return;
    if(animation==LedAnimation::FLOW_OFF) {
        resetEffectSpeed(); mode=LedMode::FLOW; animation=LedAnimation::FLOW_ON;
        lastFrameMillis=millis()-FRAME_TIME;
        Serial.println("[LED] FLOW - REVERSE FROM CURRENT POSITION"); return;
    }
    resetEffectSpeed(); mode=LedMode::FLOW; animation=LedAnimation::FLOW_ON;
    animationFront=0; lastFrameMillis=millis()-FRAME_TIME;
    Serial.println("[LED] MODE: FLOW - START");
}
void LedController::startBreathing(uint8_t r,uint8_t g,uint8_t b,uint8_t w) {
    if(mode==LedMode::BREATHING && animation==LedAnimation::BREATHING &&
       breathingR==r && breathingG==g && breathingB==b && breathingW==w) return;
    resetEffectSpeed();
    currentR=r; currentG=g; currentB=b; currentW=w;
    mode=LedMode::BREATHING; animation=LedAnimation::BREATHING;
    breathingR=r; breathingG=g; breathingB=b; breathingW=w;
    breathingCycleMs=getBreathingCycleForSpeed(); breathingStartMillis=millis();
    lastFrameMillis=millis()-FRAME_TIME;
    Serial.printf("[LED] MODE: BREATHING - START | SPEED: %u | CYCLE: %lu ms\n",effectSpeed,(unsigned long)breathingCycleMs);
}

void LedController::setColorFromHue(uint8_t hue) {
    // Pilot: 8-bit Hue, pelne nasycenie i jasnosc. Kanal W pozostaje 0.
    const float h=(float(hue)*6.0f)/256.0f;
    const uint8_t sector=(uint8_t)h;
    const float f=h-float(sector);
    const uint8_t rising=(uint8_t)(255.0f*f);
    const uint8_t falling=(uint8_t)(255.0f*(1.0f-f));
    uint8_t r=0,g=0,b=0;
    switch(sector) {
        case 0: r=255; g=rising; break;
        case 1: r=falling; g=255; break;
        case 2: g=255; b=rising; break;
        case 3: g=falling; b=255; break;
        case 4: r=rising; b=255; break;
        default: r=255; b=falling; break;
    }
    currentR=r; currentG=g; currentB=b; currentW=0;
    if(mode==LedMode::BREATHING && animation==LedAnimation::BREATHING) {
        breathingR=r; breathingG=g; breathingB=b; breathingW=0;
    } else if(mode==LedMode::FLOW && animation==LedAnimation::NONE) {
        setAllCurrentColor();
    }
    Serial.printf("[LED] COLOR HUE: %u | RGB: %u,%u,%u | W: 0\n",hue,r,g,b);
}
void LedController::turnOn() { setStaticWhite(); }
void LedController::turnOff() {
    if(mode==LedMode::FLOW) {
        if(animation==LedAnimation::FLOW_OFF) return;
        resetEffectSpeed(); animation=LedAnimation::FLOW_OFF;
        lastFrameMillis=millis()-FRAME_TIME; mode=LedMode::OFF;
        Serial.println("[LED] FLOW - STOP ANIMATION"); return;
    }
    if(mode==LedMode::STATIC_WHITE || mode==LedMode::BREATHING) {
        clear(); Serial.println("[LED] EFFECT - OFF"); return;
    }
    resetEffectSpeed();
}
void LedController::restoreMode(LedMode restoredMode) {
    switch(restoredMode) {
        case LedMode::STATIC_WHITE: setStaticWhite(); Serial.println("[LED] RESTORE: STATIC WHITE"); break;
        case LedMode::FLOW: startFlow(); Serial.println("[LED] RESTORE: FLOW"); break;
        case LedMode::BREATHING: startBreathing(); Serial.println("[LED] RESTORE: BREATHING"); break;
        case LedMode::OFF: default: clear(); Serial.println("[LED] RESTORE: OFF"); break;
    }
}
void LedController::renderFlowOn() {
    for(int i=0;i<LED_COUNT;i++) {
        const float d=animationFront-float(i);
        const float v=d<=0?0.0f:(d>=FADE_WIDTH?1.0f:smoothStep(d/FADE_WIDTH));
        strip.setPixelColor(i,makeColor((uint8_t)(currentR*v),(uint8_t)(currentG*v),(uint8_t)(currentB*v),(uint8_t)(currentW*v)));
    }
    strip.show();
}
void LedController::renderFlowOff() {
    for(int i=0;i<LED_COUNT;i++) {
        const float d=animationFront-float(i);
        const float v=d<=0?0.0f:(d>=FADE_WIDTH?1.0f:smoothStep(d/FADE_WIDTH));
        strip.setPixelColor(i,makeColor((uint8_t)(currentR*v),(uint8_t)(currentG*v),(uint8_t)(currentB*v),(uint8_t)(currentW*v)));
    }
    strip.show();
}
void LedController::renderBreathing() {
    if(!breathingCycleMs) return;
    const unsigned long elapsed=millis()-breathingStartMillis;
    const float phase=float(elapsed%breathingCycleMs)/float(breathingCycleMs);
    const float v=0.5f-0.5f*cosf(6.28318530718f*phase);
    const uint32_t c=makeColor((uint8_t)(breathingR*v),(uint8_t)(breathingG*v),(uint8_t)(breathingB*v),(uint8_t)(breathingW*v));
    for(uint16_t i=0;i<LED_COUNT;i++) strip.setPixelColor(i,c);
    strip.show();
}
void LedController::update() {
    if(animation==LedAnimation::NONE) return;
    const unsigned long now=millis();
    if(now-lastFrameMillis<FRAME_TIME) return;
    lastFrameMillis=now;
    if(animation==LedAnimation::FLOW_ON) {
        if(animationFront>=LED_COUNT+FADE_WIDTH) {
            setAllCurrentColor(); animation=LedAnimation::NONE;
            Serial.println("[LED] FLOW - COMPLETE"); return;
        }
        renderFlowOn(); animationFront+=getFlowMoveSpeed(); return;
    }
    if(animation==LedAnimation::FLOW_OFF) {
        if(animationFront<=-FADE_WIDTH) {
            clear(); Serial.println("[LED] FLOW - OFF COMPLETE"); return;
        }
        renderFlowOff(); animationFront-=getFlowMoveSpeed(); return;
    }
    if(animation==LedAnimation::BREATHING) renderBreathing();
}
