
#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// ============================================================
// TRYBY PRACY LED
// ============================================================

enum class LedMode : uint8_t
{
    OFF = 0x00,
    STATIC_WHITE = 0x01,
    FLOW = 0x02,
    BREATHING = 0x03
};

// ============================================================
// TYP ANIMACJI
// ============================================================

enum class LedAnimation : uint8_t
{
    NONE,
    FLOW_ON,
    FLOW_OFF,
    BREATHING
};

// ============================================================
// LED CONTROLLER
// ============================================================

class LedController
{
public:
    LedController();

    void begin();

    // --------------------------------------------------------
    // Polecenia sterujące
    // --------------------------------------------------------

    void turnOn();
    void turnOff();

    void setStaticWhite();
    void startFlow();

    void startBreathing(
        uint8_t r = 0,
        uint8_t g = 0,
        uint8_t b = 0,
        uint8_t w = 255
    );

    // --------------------------------------------------------
    // REGULACJA PRĘDKOŚCI EFEKTÓW
    // --------------------------------------------------------

    void increaseEffectSpeed();
    void decreaseEffectSpeed();
    void resetEffectSpeed();

    uint8_t getEffectSpeed() const;

    // --------------------------------------------------------
    // Przywracanie stanu
    // --------------------------------------------------------

    void restoreMode(LedMode mode);

    // --------------------------------------------------------
    // Obsługa animacji
    // --------------------------------------------------------

    void update();

    // --------------------------------------------------------
    // Informacje o stanie
    // --------------------------------------------------------

    void clear();

    bool isOn() const;
    LedMode getMode() const;

private:
    Adafruit_NeoPixel strip;

    LedMode mode;
    LedAnimation animation;

    float animationFront;
    unsigned long lastFrameMillis;

    // --------------------------------------------------------
    // WSPÓLNA PRĘDKOŚĆ EFEKTÓW
    // --------------------------------------------------------

    static constexpr uint8_t DEFAULT_EFFECT_SPEED = 5;
    static constexpr uint8_t MIN_EFFECT_SPEED = 1;
    static constexpr uint8_t MAX_EFFECT_SPEED = 10;

    uint8_t effectSpeed;

    // --------------------------------------------------------
    // BREATHING
    // --------------------------------------------------------

    unsigned long breathingStartMillis;
    uint32_t breathingCycleMs;

    uint8_t breathingR;
    uint8_t breathingG;
    uint8_t breathingB;
    uint8_t breathingW;

    // --------------------------------------------------------
    // Funkcje pomocnicze
    // --------------------------------------------------------

    uint32_t makeColor(
        uint8_t r,
        uint8_t g,
        uint8_t b,
        uint8_t w
    );

    float smoothStep(float x);

    void setAllWhite();

    void renderFlowOn();
    void renderFlowOff();
    void renderBreathing();
};
