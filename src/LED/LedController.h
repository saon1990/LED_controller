#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

enum class LedMode : uint8_t { OFF=0x00, STATIC_WHITE=0x01, FLOW=0x02, BREATHING=0x03 };
enum class LedAnimation : uint8_t { NONE, FLOW_ON, FLOW_OFF, BREATHING };

class LedController {
public:
    LedController();
    void begin();
    void turnOn();
    void turnOff();
    void setStaticWhite();
    void startFlow();
    void startBreathing(uint8_t r=0, uint8_t g=0, uint8_t b=0, uint8_t w=255);
    void setColorFromHue(uint8_t hue);
    void increaseEffectSpeed();
    void decreaseEffectSpeed();
    void resetEffectSpeed();
    uint8_t getEffectSpeed() const;
    void restoreMode(LedMode mode);
    void update();
    void clear();
    bool isOn() const;
    LedMode getMode() const;

private:
    Adafruit_NeoPixel strip;
    LedMode mode;
    LedAnimation animation;
    float animationFront;
    unsigned long lastFrameMillis;

    static constexpr uint8_t DEFAULT_EFFECT_SPEED=5;
    static constexpr uint8_t MIN_EFFECT_SPEED=1;
    static constexpr uint8_t MAX_EFFECT_SPEED=10;
    uint8_t effectSpeed;

    uint8_t currentR, currentG, currentB, currentW;

    unsigned long breathingStartMillis;
    uint32_t breathingCycleMs;
    uint8_t breathingR, breathingG, breathingB, breathingW;

    uint32_t getBreathingCycleForSpeed() const;
    void updateBreathingSpeed();
    float getFlowMoveSpeed() const;
    uint32_t makeColor(uint8_t r,uint8_t g,uint8_t b,uint8_t w);
    float smoothStep(float x);
    void setAllWhite();
    void setAllCurrentColor();
    void renderFlowOn();
    void renderFlowOff();
    void renderBreathing();
};
