#ifndef DMX_STATE_H
#define DMX_STATE_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include <LedEngine.h>

using LedEngineLib::AnimationMode;

// HSV Color structure (incoming MIDI data)
struct HSVColor {
    uint8_t hue;
    uint8_t saturation;
    uint8_t value;
    uint8_t white;
    
    HSVColor() : hue(0), saturation(0), value(0), white(0) {}
    HSVColor(uint8_t h, uint8_t s, uint8_t v, uint8_t w = 0) 
        : hue(h), saturation(s), value(v), white(w) {}
};

// Scene preset structure
struct ScenePreset {
    AnimationMode mode;
    HSVColor colorA;
    HSVColor colorB;
    uint8_t masterBrightness;
    uint8_t speed;
    uint8_t blendMode;
    uint8_t mirror;
    uint8_t direction;
    uint8_t animationCtrl;
    uint8_t strobeRate;
};

/**
 * DMXState - Manages the current animation state that will be sent as DMX
 */
class DMXState {
public:
    struct SceneEvent {
        bool triggered = false;
        bool saved = false;
        uint8_t sceneIndex = 0;
        bool blackout = false;
    };

    DMXState();
    ~DMXState();
    
    void begin();
    
    // MIDI handlers - convert MIDI to internal state
    void handleGlobalCC(byte controller, byte value);
    void handleColorCC(uint8_t colorBank, byte controller, byte value);
    SceneEvent handleNoteOn(byte note, byte velocity);
    void handleNoteOff(byte note);
    
    // Generate DMX frame from current state
    void toDMXFrame(uint8_t* dmxData, uint16_t size);
    
    // Getters for display
    uint8_t getMasterBrightness() const { return _masterBrightness; }
    AnimationMode getCurrentMode() const { return _currentMode; }
    uint8_t getAnimationSpeed() const { return _animationSpeed; }
    const HSVColor& getColorA() const { return _colorA; }
    const HSVColor& getColorB() const { return _colorB; }
    int8_t getCurrentScene() const { return _currentScene; }

    LedEngineLib::LedEngineState toLedEngineState() const;

private:
    struct SceneStorageBlock {
        uint32_t magic;
        ScenePreset presets[MAX_SCENES];
    };

    static constexpr uint32_t SCENE_STORAGE_MAGIC = 0x4C454453; // 'LEDS'
    static constexpr const char* SCENE_STORAGE_NAMESPACE = "dmxScenes";
    static constexpr const char* SCENE_STORAGE_KEY = "presets";

    // Current state
    AnimationMode _currentMode;
    HSVColor _colorA;
    HSVColor _colorB;
    uint8_t _masterBrightness;
    uint8_t _animationSpeed;
    uint8_t _animationCtrl;
    uint8_t _strobeRate;
    uint8_t _blendMode;
    uint8_t _mirror;
    uint8_t _direction;
    bool _sceneSaveMode;
    
    // Scene presets
    ScenePreset _scenes[MAX_SCENES];
    int8_t _currentScene;
    Preferences _preferences;
    bool _prefsReady;
    
    // Scene management
    void loadScene(uint8_t sceneIndex);
    void saveCurrentAsScene(uint8_t sceneIndex);
    void initDefaultScenes();
    bool loadScenesFromStorage();
    void persistScenes();
};

#endif // DMX_STATE_H
