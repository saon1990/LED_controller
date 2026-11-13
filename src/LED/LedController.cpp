#include "LedController.h"
#include "config.h"

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
      lastFrameMillis(0)
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
// CLEAR
// ============================================================

void LedController::clear()
{
    mode = LedMode::OFF;
    animation = LedAnimation::NONE;
    animationFront = 0.0f;

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

    setAllWhite();

    Serial.println("[LED] MODE: STATIC WHITE");
}

// ============================================================
// START FLOW
// ============================================================

void LedController::startFlow()
{
    // Jeżeli FLOW już działa lub został zakończony,
    // nie uruchamiamy go ponownie bez potrzeby.

    if (
        mode == LedMode::FLOW &&
        animation != LedAnimation::FLOW_OFF
    )
    {
        return;
    }

    mode = LedMode::FLOW;
    animation = LedAnimation::FLOW_ON;

    animationFront = 0.0f;

    // Umożliwia wykonanie pierwszej klatki
    // podczas najbliższego wywołania update().

    lastFrameMillis = millis() - FRAME_TIME;

    Serial.println("[LED] MODE: FLOW - START");
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
    // Jeżeli FLOW jest aktywny,
    // wykonujemy animację wygaszania.

    if (mode == LedMode::FLOW)
    {
        if (animation == LedAnimation::FLOW_OFF)
            return;

        animation = LedAnimation::FLOW_OFF;

        animationFront = LED_COUNT - 1;

        lastFrameMillis = millis() - FRAME_TIME;

        // Docelowy tryb jest już OFF.
        // Dzięki temu można od razu zapisać OFF w EEPROM.

        mode = LedMode::OFF;

        Serial.println("[LED] FLOW - STOP ANIMATION");

        return;
    }

    // Statyczne białe światło wyłączamy natychmiast.

    if (mode == LedMode::STATIC_WHITE)
    {
        clear();

        Serial.println("[LED] STATIC WHITE - OFF");

        return;
    }

    // Jeżeli LED już jest wyłączony,
    // nie wykonujemy żadnej dodatkowej operacji.
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

            // Efekt rozpoczynamy od początku.

            startFlow();

            Serial.println(
                "[LED] RESTORE: FLOW"
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
}