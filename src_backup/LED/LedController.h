#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class LedController
{
public:
    LedController();

    void begin();
    void turnOn();
    void turnOff();
    void restoreState(bool on);
    void clear();

    bool isOn() const;

private:
    Adafruit_NeoPixel strip;
    bool state;

    uint32_t makeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w);
    float smoothStep(float x);

    void effectOn();
    void effectOff();
};
