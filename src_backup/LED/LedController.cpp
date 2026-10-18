#include "LedController.h"
#include "config.h"

LedController::LedController()
    : strip(
          LED_COUNT,
          LED_DATA_PIN,
          NEO_GRBW + NEO_KHZ800),
      state(false)
{
}

void LedController::begin()
{
    strip.begin();
    strip.setBrightness(LED_BRIGHTNESS);
    clear();
}

bool LedController::isOn() const
{
    return state;
}

void LedController::clear()
{
    strip.clear();
    strip.show();
}

uint32_t LedController::makeColor(
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t w)
{
    // Fizyczna kolejność kanałów: R-W-G-B
    return strip.Color(r, w, g, b);
}

float LedController::smoothStep(float x)
{
    if (x <= 0.0f)
        return 0.0f;

    if (x >= 1.0f)
        return 1.0f;

    return x * x * (3.0f - 2.0f * x);
}

void LedController::effectOn()
{
    float front = 0.0f;

    while (front < LED_COUNT + FADE_WIDTH)
    {
        for (int i = 0; i < LED_COUNT; i++)
        {
            float distance = front - (float)i;
            float brightness;

            if (distance <= 0.0f)
                brightness = 0.0f;
            else if (distance >= FADE_WIDTH)
                brightness = 1.0f;
            else
                brightness = smoothStep(
                    distance / FADE_WIDTH);

            uint8_t level =
                (uint8_t)(brightness * 255.0f);

            strip.setPixelColor(
                i,
                makeColor(0, 0, 0, level));
        }

        strip.show();

        front += MOVE_SPEED;
        delay(FRAME_TIME);
    }

    for (int i = 0; i < LED_COUNT; i++)
    {
        strip.setPixelColor(
            i,
            makeColor(0, 0, 0, 255));
    }

    strip.show();
}

void LedController::effectOff()
{
    float front = LED_COUNT - 1;

    while (front > -FADE_WIDTH)
    {
        for (int i = 0; i < LED_COUNT; i++)
        {
            float distance = front - (float)i;
            float brightness;

            if (distance <= 0.0f)
                brightness = 0.0f;
            else if (distance >= FADE_WIDTH)
                brightness = 1.0f;
            else
                brightness = smoothStep(
                    distance / FADE_WIDTH);

            uint8_t level =
                (uint8_t)(brightness * 255.0f);

            strip.setPixelColor(
                i,
                makeColor(0, 0, 0, level));
        }

        strip.show();

        front -= MOVE_SPEED;
        delay(FRAME_TIME);
    }

    clear();
}

void LedController::turnOn()
{
    if (state)
        return;

    state = true;
    effectOn();
}

void LedController::turnOff()
{
    if (!state)
    {
        clear();
        return;
    }

    state = false;
    effectOff();
}

void LedController::restoreState(bool on)
{
    state = on;

    if (!on)
        clear();
}
