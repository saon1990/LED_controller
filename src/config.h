
#pragma once

#include <Arduino.h>

// LED / WS2814
constexpr uint8_t LED_DATA_PIN = 1;
constexpr uint16_t LED_COUNT = 70;
constexpr uint8_t LED_BRIGHTNESS = 255;

// Efekt FLOW
constexpr uint16_t FRAME_TIME = 20;
constexpr float MOVE_SPEED = 0.35f;
constexpr float FADE_WIDTH = 8.0f;

// Efekt BREATHING
constexpr uint32_t BREATHING_CYCLE_MS = 6000UL;

// I2C
constexpr uint8_t I2C_SDA = 3;
constexpr uint8_t I2C_SCL = 10;

constexpr uint8_t RTC_ADDR = 0x68;
constexpr uint8_t EEPROM_ADDR = 0x57;

constexpr uint16_t EEPROM_TIMESTAMP_ADDR = 0x0010;
constexpr uint16_t EEPROM_LED_STATE_ADDR = 0x0014;
constexpr uint16_t EEPROM_LED_VALID_ADDR = 0x0015;
constexpr uint16_t EEPROM_LAST_ACTIVE_MODE_ADDR = 0x0016;

constexpr uint8_t LED_VALID_MARKER = 0xA5;

constexpr unsigned long SAVE_INTERVAL = 60UL;
constexpr unsigned long LONG_OUTAGE = 300UL;

// RainMaker
constexpr const char* SERVICE_NAME = "PROV_LED_TARAS";
constexpr const char* POP = "LED12345";

// Sieć
constexpr uint8_t STATIC_IP[4] = {192, 168, 1, 75};
constexpr uint8_t GATEWAY[4] = {192, 168, 1, 1};
constexpr uint8_t SUBNET[4] = {255, 255, 255, 0};
constexpr uint8_t DNS[4] = {192, 168, 1, 1};
