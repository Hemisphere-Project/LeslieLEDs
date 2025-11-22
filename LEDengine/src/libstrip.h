#pragma once

#include <stdint.h>

struct pixelColor_t {
    union {
        struct __attribute__((packed)) {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            uint8_t w;
        };
        uint32_t num;
    };
};

struct strand_t {
    int rmtChannel = 0;
    int gpioNum = 0;
    int ledType = 0;
    int brightLimit = 255;
    int numPixels = 0;
    pixelColor_t* pixels = nullptr;
    void* _stateVars = nullptr;
};

enum led_order {
    S_RGB,
    S_RBG,
    S_GRB,
    S_GBR,
    S_BRG,
    S_BGR
};

struct ledParams_t {
    int bytesPerPixel;
    led_order ledOrder;
    uint32_t T0H;
    uint32_t T1H;
    uint32_t T0L;
    uint32_t T1L;
    uint32_t TRS;
};

enum led_types {
    LED_WS2812_V1,
    LED_WS2812B_V1,
    LED_WS2812B_V2,
    LED_WS2812B_V3,
    LED_WS2813_V1,
    LED_WS2813_V2,
    LED_WS2813_V3,
    LED_WS2813_V4,
    LED_WS2815_V1,
    LED_SK6812_V1,
    LED_SK6812W_V1,
    LED_SK6812W_V3,
    LED_SK6812W_V4,
    LED_TM1934,
};

class LibStrip {
public:
    static int init();
    static strand_t* addStrand(const strand_t& strand);
    static int updatePixels(strand_t* strand);
    static void resetStrand(strand_t* strand);
};

inline uint8_t scale8(uint8_t value, uint8_t scale) {
    return static_cast<uint8_t>((static_cast<uint16_t>(value) * (static_cast<uint16_t>(scale) + 1)) >> 8);
}

inline uint8_t lerp8by8(uint8_t start, uint8_t end, uint8_t frac) {
    int16_t delta = static_cast<int16_t>(end) - static_cast<int16_t>(start);
    int16_t scaled = static_cast<int16_t>((delta * frac) >> 8);
    int16_t result = static_cast<int16_t>(start) + scaled;
    if (result < 0) return 0;
    if (result > 255) return 255;
    return static_cast<uint8_t>(result);
}

struct CRGBW {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t w;

    CRGBW() : r(0), g(0), b(0), w(0) {}
    CRGBW(uint8_t red, uint8_t green, uint8_t blue, uint8_t white = 0)
        : r(red), g(green), b(blue), w(white) {}
};
