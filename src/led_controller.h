#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include <FastLED.h>
#include "config.h"

// Color structure for RGBW
struct ColorRGBW {
    uint8_t r, g, b, w;
    
    ColorRGBW() : r(0), g(0), b(0), w(0) {}
    ColorRGBW(uint8_t red, uint8_t green, uint8_t blue, uint8_t white = 0) 
        : r(red), g(green), b(blue), w(white) {}
    
    void fromHSV(uint8_t hue, uint8_t sat, uint8_t val, uint8_t white = 0);
    CRGB toCRGB() const { return CRGB(r, g, b); }
};

// Scene preset structure
struct ScenePreset {
    AnimationMode mode;
    ColorRGBW colorA;
    ColorRGBW colorB;
    uint8_t speed;
    uint8_t blendMode;
    bool mirror;
    bool reverse;
    uint8_t segmentSize;
};

class LEDController {
public:
    LEDController();
    
    void begin();
    void update();
    
    // MIDI handlers
    void handleGlobalCC(byte controller, byte value);
    void handleColorCC(uint8_t colorBank, byte controller, byte value);
    void handleNoteOn(byte note, byte velocity);
    void handleNoteOff(byte note);
    
    // Getters for display
    uint8_t getMasterBrightness() const { return _masterBrightness; }
    AnimationMode getCurrentMode() const { return _currentMode; }
    uint8_t getAnimationSpeed() const { return _animationSpeed; }
    const ColorRGBW& getColorA() const { return _colorA; }
    const ColorRGBW& getColorB() const { return _colorB; }
    uint32_t getFPS() const { return _fps; }

private:
    CRGB _leds[LED_COUNT];
    
    // Animation state
    AnimationMode _currentMode;
    ColorRGBW _colorA;
    ColorRGBW _colorB;
    uint8_t _masterBrightness;
    uint8_t _animationSpeed;
    uint8_t _strobeRate;
    uint8_t _blendMode;
    bool _mirror;
    bool _reverse;
    uint8_t _segmentSize;
    
    // Timing
    unsigned long _lastUpdate;
    unsigned long _frameCount;
    unsigned long _fpsTimer;
    uint32_t _fps;
    uint32_t _animationPhase;
    
    // Scene presets
    ScenePreset _scenes[MAX_SCENES];
    int8_t _currentScene;
    
    // Animation functions
    void renderSolid();
    void renderDualSolid();
    void renderChase();
    void renderDash();
    void renderStrobe();
    void renderPulse();
    void renderRainbow();
    void renderSparkle();
    
    // Utility
    void setPixelRGBW(uint16_t index, const ColorRGBW& color);
    void applyMirror();
    void applyBrightness();
    void calculateFPS();
    
    // Scene management
    void loadScene(uint8_t sceneIndex);
    void saveCurrentAsScene(uint8_t sceneIndex);
    void initDefaultScenes();
};

#endif // LED_CONTROLLER_H
