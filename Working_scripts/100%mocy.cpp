#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_DATA_PIN 1
#define LED_COUNT    70

Adafruit_NeoPixel strip(
    LED_COUNT,
    LED_DATA_PIN,
    NEO_GRBW + NEO_KHZ800
);

void setup()
{
    Serial.begin(115200);

    strip.begin();
    strip.setBrightness(255);
    strip.clear();

    // Pełna moc białego kanału
    for (int i = 0; i < LED_COUNT; i++)
    {
        strip.setPixelColor(
            i,
            strip.Color(0, 255, 0, 0)
        );
    }

    strip.show();

    Serial.println("WHITE 100%");
}

void loop()
{
}