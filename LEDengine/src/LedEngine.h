#pragma once

#include <Arduino.h>
#include <FastLED.h>

#ifndef LEDENGINE_CHIPSET
#define LEDENGINE_CHIPSET SK6812
#endif

#ifndef LEDENGINE_COLOR_ORDER
#define LEDENGINE_COLOR_ORDER GRB
#endif

#ifndef LEDENGINE_DATA_PIN
#ifdef LED_DATA_PIN
#define LEDENGINE_DATA_PIN LED_DATA_PIN
#else
#define LEDENGINE_DATA_PIN 2
#endif
#endif

namespace LedEngineLib {

struct ColorRGBW {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t w;

    ColorRGBW() : r(0), g(0), b(0), w(0) {}
    ColorRGBW(uint8_t red, uint8_t green, uint8_t blue, uint8_t white = 0)
        : r(red), g(green), b(blue), w(white) {}

    void fromHSV(uint8_t hue, uint8_t sat, uint8_t val, uint8_t white = 0);
    CRGB toCRGB() const { return CRGB(r, g, b); }
};

struct CRGBW {
    uint8_t g;
    uint8_t r;
    uint8_t b;
    uint8_t w;
};

enum AnimationMode {
    ANIM_SOLID = 0,
    ANIM_DUAL_SOLID,
    ANIM_CHASE,
    ANIM_DASH,
    ANIM_WAVEFORM,
    ANIM_PULSE,
    ANIM_RAINBOW,
    ANIM_SPARKLE,
    ANIM_CUSTOM_1,
    ANIM_CUSTOM_2,
    ANIM_MODE_COUNT
};

enum MirrorMode {
    MIRROR_NONE = 0,
    MIRROR_FULL,
    MIRROR_SPLIT2,
    MIRROR_SPLIT3,
    MIRROR_SPLIT4
};

enum DirectionMode {
    DIR_FORWARD = 0,
    DIR_BACKWARD,
    DIR_PINGPONG,
    DIR_RANDOM
};

enum WaveformType {
    WAVE_SINE = 0,
    WAVE_TRIANGLE,
    WAVE_SQUARE,
    WAVE_SAWTOOTH
};

struct LedEngineConfig {
    uint16_t ledCount = 0;
    uint8_t dataPin = 2;
    uint8_t targetFPS = 60;
    uint8_t defaultBrightness = 128;
    bool enableRGBW = true;
};

struct LedEngineState {
    uint8_t masterBrightness = 0;
    AnimationMode mode = ANIM_SOLID;
    uint8_t animationSpeed = 0;
    uint8_t animationCtrl = 0;
    uint8_t strobeRate = 0;
    uint8_t blendMode = 0;
    MirrorMode mirror = MIRROR_NONE;
    DirectionMode direction = DIR_FORWARD;
    ColorRGBW colorA;
    ColorRGBW colorB;
};

class LedEngine {
public:
    explicit LedEngine(const LedEngineConfig& config);
    ~LedEngine();

    bool begin();
    void update(uint32_t clockMillis, const LedEngineState& state);
    void show();

    uint16_t getLedCount() const { return _config.ledCount; }
    uint8_t getFPS() const { return _fps; }
    const LedEngineState& getState() const { return _state; }
    const CRGB* getPreviewPixels() const;

private:
    LedEngineConfig _config;
    LedEngineState _state;

    CRGBW* _leds;
    mutable CRGB* _previewBuffer;
    bool _initialized;
    uint32_t _animationPhase;
    uint32_t _lastUpdateClock;
    uint32_t _frameIntervalMs;
    uint32_t _frameCount;
    uint32_t _fpsTimer;
    uint8_t _fps;

    void renderFrame(uint32_t clockMillis);
    void renderSolid();
    void renderDualSolid();
    void renderChase();
    void renderDash();
    void renderWaveform();
    void renderPulse(uint32_t clockMillis);
    void renderRainbow();
    void renderSparkle();

    void setPixelRGBW(uint16_t index, const ColorRGBW& color);
    void clearLEDs();
    void fadePixel(uint16_t index, uint8_t amount);
    void applyMirror();
    void applyStrobeOverlay(uint32_t clockMillis);
    void calculateFPS();
    WaveformType currentWaveform() const;
};

} // namespace LedEngineLib
