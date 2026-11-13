
#include "LedController.h"
#include "config.h"

#include <math.h>

// ============================================================
// CONSTRUCTOR
// ============================================================

LedController::LedController()
    : strip(
          LED_COUNT,
          LED_DATA_PIN,
          NEO_GRBW + NEO_KHZ800),
      mode(LedMode::OFF),
      animation(LedAnimation::NONE),
      animationFront(0.0f),
      lastFrameMillis(0),
      effectSpeed(DEFAULT_EFFECT_SPEED),
      breathingStartMillis(0),
      breathingCycleMs(BREATHING_CYCLE_MS),
      breathingR(0),
      breathingG(0),
      breathingB(0),
      breathingW(255)
{
}

// ============================================================
// BEGIN
// ============================================================

void LedController::begin()
{
    strip.begin();
    strip.setBrightness(LED_BRIGHTNESS);

    clear();
}

// ============================================================
// STATUS
// ============================================================

bool LedController::isOn() const
{
    return mode != LedMode::OFF;
}

// ============================================================
// CURRENT MODE
// ============================================================

LedMode LedController::getMode() const
{
    return mode;
}

// ============================================================
// EFFECT SPEED
// ============================================================

void LedController::resetEffectSpeed()
{
    effectSpeed = DEFAULT_EFFECT_SPEED;

    Serial.printf(
        "[LED] EFFECT SPEED RESET: %u\n",
        effectSpeed
    );
}

void LedController::increaseEffectSpeed()
{
    if (
        mode != LedMode::FLOW &&
        mode != LedMode::BREATHING
    )
    {
        Serial.println(
            "[LED] SPEED_PLUS - brak aktywnego efektu."
        );

        return;
    }

    if (effectSpeed >= MAX_EFFECT_SPEED)
    {
        Serial.printf(
            "[LED] EFFECT SPEED: MAX (%u)\n",
            effectSpeed
        );

        return;
    }

    effectSpeed++;

    Serial.printf(
        "[LED] EFFECT SPEED: %u/%u\n",
        effectSpeed,
        MAX_EFFECT_SPEED
    );
}

void LedController::decreaseEffectSpeed()
{
    if (
        mode != LedMode::FLOW &&
        mode != LedMode::BREATHING
    )
    {
        Serial.println(
            "[LED] SPEED_MINUS - brak aktywnego efektu."
        );

        return;
    }

    if (effectSpeed <= MIN_EFFECT_SPEED)
    {
        Serial.printf(
            "[LED] EFFECT SPEED: MIN (%u)\n",
            effectSpeed
        );

        return;
    }

    effectSpeed--;

    Serial.printf(
        "[LED] EFFECT SPEED: %u/%u\n",
        effectSpeed,
        MAX_EFFECT_SPEED
    );
}

uint8_t LedController::getEffectSpeed() const
{
    return effectSpeed;
}

// ============================================================
// CLEAR
// ============================================================

void LedController::clear()
{
    mode = LedMode::OFF;
    animation = LedAnimation::NONE;
    animationFront = 0.0f;

    resetEffectSpeed();

    strip.clear();
    strip.show();
}

// ============================================================
// COLOR
// ============================================================

uint32_t LedController::makeColor(
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t w)
{
    // Fizyczna kolejność kanałów WS2814: R-W-G-B
    return strip.Color(r, w, g, b);
}

// ============================================================
// SMOOTH STEP
// ============================================================

float LedController::smoothStep(float x)
{
    if (x <= 0.0f)
        return 0.0f;

    if (x >= 1.0f)
        return 1.0f;

    return x * x * (3.0f - 2.0f * x);
}

// ============================================================
// SET ALL PIXELS WHITE
// ============================================================

void LedController::setAllWhite()
{
    for (uint16_t i = 0; i < LED_COUNT; i++)
    {
        strip.setPixelColor(
            i,
            makeColor(0, 0, 0, 255)
        );
    }

    strip.show();
}

// ============================================================
// STATIC WHITE
// ============================================================

void LedController::setStaticWhite()
{
    mode = LedMode::STATIC_WHITE;
    animation = LedAnimation::NONE;

    animationFront = 0.0f;

    resetEffectSpeed();

    setAllWhite();

    Serial.println("[LED] MODE: STATIC WHITE");
}

// ============================================================
// START FLOW
// ============================================================

void LedController::startFlow()
{
    // Jeżeli FLOW jest już aktywny,
    // nie uruchamiamy go ponownie.

    if (
        mode == LedMode::FLOW &&
        animation != LedAnimation::FLOW_OFF
    )
    {
        return;
    }

    // --------------------------------------------------------
    // REVERSE FLOW OFF
    // --------------------------------------------------------

    if (animation == LedAnimation::FLOW_OFF)
    {
        resetEffectSpeed();

        mode = LedMode::FLOW;
        animation = LedAnimation::FLOW_ON;

        lastFrameMillis = millis() - FRAME_TIME;

        Serial.println(
            "[LED] FLOW - REVERSE FROM CURRENT POSITION"
        );

        return;
    }

    // --------------------------------------------------------
    // NEW FLOW
    // --------------------------------------------------------

    resetEffectSpeed();

    mode = LedMode::FLOW;
    animation = LedAnimation::FLOW_ON;

    animationFront = 0.0f;

    lastFrameMillis = millis() - FRAME_TIME;

    Serial.println("[LED] MODE: FLOW - START");
}

// ============================================================
// START BREATHING
// ============================================================

void LedController::startBreathing(
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t w)
{
    // Jeżeli BREATHING już działa z tym samym kolorem,
    // nie restartujemy animacji ani prędkości.

    if (
        mode == LedMode::BREATHING &&
        animation == LedAnimation::BREATHING &&
        breathingR == r &&
        breathingG == g &&
        breathingB == b &&
        breathingW == w
    )
    {
        return;
    }

    resetEffectSpeed();

    mode = LedMode::BREATHING;
    animation = LedAnimation::BREATHING;

    breathingR = r;
    breathingG = g;
    breathingB = b;
    breathingW = w;

    breathingCycleMs = BREATHING_CYCLE_MS;
    breathingStartMillis = millis();

    lastFrameMillis = millis() - FRAME_TIME;

    Serial.println("[LED] MODE: BREATHING - START");
}

// ============================================================
// TURN ON
// ============================================================

void LedController::turnOn()
{
    setStaticWhite();
}

// ============================================================
// TURN OFF
// ============================================================

void LedController::turnOff()
{
    // --------------------------------------------------------
    // FLOW OFF
    // --------------------------------------------------------

    if (mode == LedMode::FLOW)
    {
        if (animation == LedAnimation::FLOW_OFF)
            return;

        resetEffectSpeed();

        animation = LedAnimation::FLOW_OFF;

        lastFrameMillis = millis() - FRAME_TIME;

        mode = LedMode::OFF;

        Serial.println("[LED] FLOW - STOP ANIMATION");

        return;
    }

    // --------------------------------------------------------
    // STATIC WHITE / BREATHING OFF
    // --------------------------------------------------------

    if (
        mode == LedMode::STATIC_WHITE ||
        mode == LedMode::BREATHING
    )
    {
        clear();

        Serial.println("[LED] EFFECT - OFF");

        return;
    }

    // Jeżeli LED są już wyłączone, również resetujemy prędkość.
    resetEffectSpeed();
}

// ============================================================
// RESTORE MODE
// ============================================================

void LedController::restoreMode(LedMode restoredMode)
{
    switch (restoredMode)
    {
        case LedMode::STATIC_WHITE:

            setStaticWhite();

            Serial.println(
                "[LED] RESTORE: STATIC WHITE"
            );

            break;

        case LedMode::FLOW:

            startFlow();

            Serial.println(
                "[LED] RESTORE: FLOW"
            );

            break;

        case LedMode::BREATHING:

            startBreathing();

            Serial.println(
                "[LED] RESTORE: BREATHING"
            );

            break;

        case LedMode::OFF:

        default:

            clear();

            Serial.println(
                "[LED] RESTORE: OFF"
            );

            break;
    }
}

// ============================================================
// RENDER FLOW ON
// ============================================================

void LedController::renderFlowOn()
{
    for (int i = 0; i < LED_COUNT; i++)
    {
        float distance =
            animationFront - (float)i;

        float brightness;

        if (distance <= 0.0f)
        {
            brightness = 0.0f;
        }
        else if (distance >= FADE_WIDTH)
        {
            brightness = 1.0f;
        }
        else
        {
            brightness = smoothStep(
                distance / FADE_WIDTH
            );
        }

        uint8_t level =
            (uint8_t)(brightness * 255.0f);

        strip.setPixelColor(
            i,
            makeColor(0, 0, 0, level)
        );
    }

    strip.show();
}

// ============================================================
// RENDER FLOW OFF
// ============================================================

void LedController::renderFlowOff()
{
    for (int i = 0; i < LED_COUNT; i++)
    {
        float distance =
            animationFront - (float)i;

        float brightness;

        if (distance <= 0.0f)
        {
            brightness = 0.0f;
        }
        else if (distance >= FADE_WIDTH)
        {
            brightness = 1.0f;
        }
        else
        {
            brightness = smoothStep(
                distance / FADE_WIDTH
            );
        }

        uint8_t level =
            (uint8_t)(brightness * 255.0f);

        strip.setPixelColor(
            i,
            makeColor(0, 0, 0, level)
        );
    }

    strip.show();
}

// ============================================================
// RENDER BREATHING
// ============================================================

void LedController::renderBreathing()
{
    if (breathingCycleMs == 0)
        return;

    const unsigned long elapsed =
        millis() - breathingStartMillis;

    const float phase =
        static_cast<float>(elapsed % breathingCycleMs) /
        static_cast<float>(breathingCycleMs);

    const float brightness =
        0.5f - 0.5f * cosf(
            6.28318530718f * phase
        );

    const uint8_t r =
        static_cast<uint8_t>(breathingR * brightness);

    const uint8_t g =
        static_cast<uint8_t>(breathingG * brightness);

    const uint8_t b =
        static_cast<uint8_t>(breathingB * brightness);

    const uint8_t w =
        static_cast<uint8_t>(breathingW * brightness);

    const uint32_t color =
        makeColor(r, g, b, w);

    for (uint16_t i = 0; i < LED_COUNT; i++)
    {
        strip.setPixelColor(i, color);
    }

    strip.show();
}

// ============================================================
// UPDATE
// ============================================================

void LedController::update()
{
    if (animation == LedAnimation::NONE)
        return;

    unsigned long now = millis();

    if (
        now - lastFrameMillis <
        FRAME_TIME
    )
    {
        return;
    }

    lastFrameMillis = now;

    // --------------------------------------------------------
    // FLOW ON
    // --------------------------------------------------------

    if (animation == LedAnimation::FLOW_ON)
    {
        if (
            animationFront >=
            LED_COUNT + FADE_WIDTH
        )
        {
            setAllWhite();

            animation = LedAnimation::NONE;

            Serial.println(
                "[LED] FLOW - COMPLETE"
            );

            return;
        }

        renderFlowOn();

        animationFront += MOVE_SPEED;

        return;
    }

    // --------------------------------------------------------
    // FLOW OFF
    // --------------------------------------------------------

    if (animation == LedAnimation::FLOW_OFF)
    {
        if (
            animationFront <= -FADE_WIDTH
        )
        {
            clear();

            Serial.println(
                "[LED] FLOW - OFF COMPLETE"
            );

            return;
        }

        renderFlowOff();

        animationFront -= MOVE_SPEED;

        return;
    }

    // --------------------------------------------------------
    // BREATHING
    // --------------------------------------------------------

    if (animation == LedAnimation::BREATHING)
    {
        renderBreathing();
        return;
    }
}
