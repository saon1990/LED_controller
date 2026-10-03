#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_DATA_PIN 1
#define LED_COUNT    70

// Parametry efektu
#define FRAME_TIME   20      // ms między klatkami
#define MOVE_SPEED   0.35f   // ile segmentu przesuwamy na klatkę
#define FADE_WIDTH   8.0f    // szerokość płynnego przejścia

Adafruit_NeoPixel strip(
    LED_COUNT,
    LED_DATA_PIN,
    NEO_GRBW + NEO_KHZ800
);


// ------------------------------------------------------------
// Kolor RGBW
// U Ciebie kolejność fizyczna jest R-W-G-B,
// dlatego tutaj normalnie podajemy RGBW,
// a funkcja robi odpowiednie mapowanie.
// ------------------------------------------------------------
uint32_t makeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    return strip.Color(r, w, g, b);
}


// ------------------------------------------------------------
// Smoothstep 0.0 ... 1.0
// Dzięki temu przejście nie jest liniowe i wygląda bardziej
// naturalnie.
// ------------------------------------------------------------
float smoothStep(float x)
{
    if (x <= 0.0f)
        return 0.0f;

    if (x >= 1.0f)
        return 1.0f;

    return x * x * (3.0f - 2.0f * x);
}


// ------------------------------------------------------------
// ON
//
// Front przesuwa się od 0 -> LED_COUNT.
//
// Przed frontem       = 0%
// Front               = płynne przejście
// Za frontem          = 100%
// ------------------------------------------------------------
void effectOn()
{
    float front = 0.0f;

    while (front < LED_COUNT + FADE_WIDTH)
    {
        for (int i = 0; i < LED_COUNT; i++)
        {
            float distance = front - (float)i;

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
                brightness = smoothStep(distance / FADE_WIDTH);
            }

            uint8_t level = (uint8_t)(brightness * 255.0f);

            strip.setPixelColor(
                i,
                makeColor(0, 0, 0, level)
            );
        }

        strip.show();

        front += MOVE_SPEED;

        delay(FRAME_TIME);
    }

    // Upewniamy się, że wszystko jest 100%
    for (int i = 0; i < LED_COUNT; i++)
    {
        strip.setPixelColor(
            i,
            makeColor(0, 0, 0, 255)
        );
    }

    strip.show();
}


// ------------------------------------------------------------
// OFF
//
// !!! TO JEST KLUCZOWA RÓŻNICA !!!
//
// Startujemy od końca:
// 100% 100% 100% 100% ...
//
// Front przesuwa się od LED_COUNT -> 0.
//
// Za frontem (od strony końca) = 0%
// Front                  = płynne wygaszanie
// Przed frontem          = 100%
//
// Czyli segmenty GASZĄ SIĘ,
// a nie rozpalają od drugiego końca.
// ------------------------------------------------------------
void effectOff()
{
    float front = LED_COUNT - 1;

    while (front > -FADE_WIDTH)
    {
        for (int i = 0; i < LED_COUNT; i++)
        {
            float distance = front - (float)i;

            float brightness;

            if (distance <= 0.0f)
            {
                // Segment znajduje się już za frontem
                // w kierunku wygaszania -> 0%
                brightness = 0.0f;
            }
            else if (distance >= FADE_WIDTH)
            {
                // Segment jeszcze nie został wygaszony -> 100%
                brightness = 1.0f;
            }
            else
            {
                // Płynne wygaszanie
                brightness = smoothStep(distance / FADE_WIDTH);
            }

            uint8_t level = (uint8_t)(brightness * 255.0f);

            strip.setPixelColor(
                i,
                makeColor(0, 0, 0, level)
            );
        }

        strip.show();

        front -= MOVE_SPEED;

        delay(FRAME_TIME);
    }

    // Upewniamy się, że wszystko jest OFF
    strip.clear();
    strip.show();
}


void setup()
{
    Serial.begin(115200);
    delay(1000);

    strip.begin();
    strip.clear();
    strip.show();

    Serial.println("LED EFFECT V3");

    // Test ON
    effectOn();

    // Całość świeci przez 2 sekundy
    delay(2000);

    // Test OFF
    effectOff();
}


void loop()
{
}