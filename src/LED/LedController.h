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
    FLOW = 0x02
};

// ============================================================
// TYP ANIMACJI
// ============================================================

enum class LedAnimation : uint8_t
{
    NONE,
    FLOW_ON,
    FLOW_OFF
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
};