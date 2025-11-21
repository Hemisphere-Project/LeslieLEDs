#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include <FastLED.h>
#include "config.h"

// Forward declarations
class DisplayHandler;

// Mirror modes
enum MirrorMode {
    MIRROR_NONE = 0,        // No mirroring
    MIRROR_FULL,            // Full strip mirror
    MIRROR_SPLIT2,          // Quarter segments with alternating reverse
    MIRROR_SPLIT3,          // Sixth segments with alternating reverse
    MIRROR_SPLIT4           // Eighth segments with alternating reverse
};

// Direction modes
enum DirectionMode {
    DIR_FORWARD = 0,        // Normal forward direction
    DIR_BACKWARD,           // Reversed direction
    DIR_PINGPONG,           // Back and forth (snap back to start)
    DIR_RANDOM              // Random frame in animation
};

// Waveform types for ANIM_WAVEFORM
enum WaveformType {
    WAVE_SINE = 0,          // Smooth sine wave
    WAVE_TRIANGLE,          // Linear triangle wave
    WAVE_SQUARE,            // Hard on/off square wave
    WAVE_SAWTOOTH           // Linear ramp sawtooth
};

// Color structure for RGBW
struct ColorRGBW {
    uint8_t r, g, b, w;
    
    ColorRGBW() : r(0), g(0), b(0), w(0) {}
    ColorRGBW(uint8_t red, uint8_t green, uint8_t blue, uint8_t white = 0) 
        : r(red), g(green), b(blue), w(white) {}
    
    void fromHSV(uint8_t hue, uint8_t sat, uint8_t val, uint8_t white = 0);
    CRGB toCRGB() const { return CRGB(r, g, b); }
};

// CRGBW structure for raw LED data (4 bytes per pixel: G, R, B, W)
struct CRGBW {
    uint8_t g;
    uint8_t r;
    uint8_t b;
    uint8_t w;
};

// Scene preset structure
struct ScenePreset {
    AnimationMode mode;
    ColorRGBW colorA;
    ColorRGBW colorB;
    uint8_t speed;
    uint8_t blendMode;
    MirrorMode mirror;
    DirectionMode direction;
    uint8_t animationCtrl;
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
    
    // Display handler
    void setDisplayHandler(DisplayHandler* display);
    
    // Getters for display
    uint8_t getMasterBrightness() const { return _masterBrightness; }
    AnimationMode getCurrentMode() const { return _currentMode; }
    uint8_t getAnimationSpeed() const { return _animationSpeed; }
    uint8_t getStrobeRate() const { return _strobeRate; }
    const ColorRGBW& getColorA() const { return _colorA; }
    const ColorRGBW& getColorB() const { return _colorB; }
    uint32_t getFPS() const { return _fps; }
    const CRGB* getLEDs() const { 
        // Convert CRGBW to CRGB for display preview
        static CRGB displayBuffer[LED_COUNT];
        for(int i = 0; i < LED_COUNT; i++) {
            displayBuffer[i].r = _leds[i].r;
            displayBuffer[i].g = _leds[i].g;
            displayBuffer[i].b = _leds[i].b;
        }
        return displayBuffer;
    }

private:
    CRGBW _leds[LED_COUNT];  // Raw RGBW data (4 bytes per pixel)
    DisplayHandler* _displayHandler;
    
    // Animation state
    AnimationMode _currentMode;
    ColorRGBW _colorA;
    ColorRGBW _colorB;
    uint8_t _masterBrightness;
    uint8_t _animationSpeed;
    uint8_t _animationCtrl;
    uint8_t _strobeRate;
    uint8_t _blendMode;
    MirrorMode _mirror;
    DirectionMode _direction;
    WaveformType _waveform;
    bool _sceneSaveMode;
    
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
    void renderWaveform();
    void renderPulse();
    void renderRainbow();
    void renderSparkle();
    
    // Utility
    void setPixelRGBW(uint16_t index, const ColorRGBW& color);
    void clearLEDs();
    void fadePixel(uint16_t index, uint8_t amount);
    void applyMirror();
    void applyStrobeOverlay();
    void applyBrightness();
    void calculateFPS();
    
    // Scene management
    void loadScene(uint8_t sceneIndex);
    void saveCurrentAsScene(uint8_t sceneIndex);
    void initDefaultScenes();
};

#endif // LED_CONTROLLER_H
