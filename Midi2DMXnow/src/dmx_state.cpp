#include "dmx_state.h"

using LedEngineLib::ColorRGBW;
using LedEngineLib::DirectionMode;
using LedEngineLib::LedEngineState;
using LedEngineLib::MirrorMode;

namespace {

MirrorMode decodeMirror(uint8_t value) {
    if (value < 51) return LedEngineLib::MIRROR_NONE;
    if (value < 102) return LedEngineLib::MIRROR_FULL;
    if (value < 153) return LedEngineLib::MIRROR_SPLIT2;
    if (value < 204) return LedEngineLib::MIRROR_SPLIT3;
    return LedEngineLib::MIRROR_SPLIT4;
}

DirectionMode decodeDirection(uint8_t value) {
    if (value < 64) return LedEngineLib::DIR_FORWARD;
    if (value < 128) return LedEngineLib::DIR_BACKWARD;
    if (value < 192) return LedEngineLib::DIR_PINGPONG;
    return LedEngineLib::DIR_RANDOM;
}

}

DMXState::DMXState() 
    : _currentMode(LedEngineLib::ANIM_SOLID)
    , _masterBrightness(128)
    , _animationSpeed(64)
    , _animationCtrl(0)
    , _strobeRate(0)
    , _blendMode(0)
    , _mirror(0)
    , _direction(0)
    , _sceneSaveMode(false)
    , _currentScene(-1)
    , _prefsReady(false)
{
    // Initialize with default colors (HSV format)
    _colorA = HSVColor(0, 255, 255, 0);      // Red
    _colorB = HSVColor(160, 255, 255, 0);    // Cyan
}

DMXState::~DMXState() {
    if (_prefsReady) {
        _preferences.end();
        _prefsReady = false;
    }
}

void DMXState::begin() {
    if (!_prefsReady) {
        _prefsReady = _preferences.begin(SCENE_STORAGE_NAMESPACE, false);
    }

    initDefaultScenes();

    if (!loadScenesFromStorage()) {
        persistScenes();
    }
    
    #if DEBUG_MODE
    Serial.println("DMX State initialized");
    Serial.printf("Default mode: ANIM_SOLID, Color A (red HSV)\n");
    #endif
}

void DMXState::handleGlobalCC(byte controller, byte value) {
    switch (controller) {
        case CC_MASTER_BRIGHTNESS:
            _masterBrightness = map(value, 0, 127, 0, 255);
            break;
            
        case CC_ANIMATION_SPEED:
            _animationSpeed = map(value, 0, 127, 0, 255);
            break;
            
        case CC_ANIMATION_CTRL:
            _animationCtrl = map(value, 0, 127, 0, 255);
            break;
            
        case CC_STROBE_RATE:
            _strobeRate = map(value, 0, 127, 0, 255);
            break;
            
        case CC_BLEND_MODE:
            _blendMode = map(value, 0, 127, 0, 255);
            break;
            
        case CC_MIRROR_MODE:
            _mirror = map(value, 0, 127, 0, 255);
            break;
            
        case CC_DIRECTION:
            _direction = map(value, 0, 127, 0, 255);
            break;
            
        case CC_ANIMATION_MODE:
            // Map 0-127 to animation modes
            {
                uint8_t mode = value / (128 / LedEngineLib::ANIM_MODE_COUNT);
                if (mode >= LedEngineLib::ANIM_MODE_COUNT) {
                    mode = LedEngineLib::ANIM_MODE_COUNT - 1;
                }
                _currentMode = static_cast<AnimationMode>(mode);
            }
            break;
            
        case CC_SCENE_SAVE_MODE:
            // Require a bit of headroom to avoid noisy knob flickers enabling save mode accidentally
            _sceneSaveMode = (value >= 64);
            break;
    }
}

void DMXState::handleColorCC(uint8_t colorBank, byte controller, byte value) {
    HSVColor* targetColor = (colorBank == 0) ? &_colorA : &_colorB;
    
    switch (controller) {
        case CC_COLOR_A_HUE:
        case CC_COLOR_B_HUE:
            targetColor->hue = map(value, 0, 127, 0, 255);
            break;
            
        case CC_COLOR_A_SATURATION:
        case CC_COLOR_B_SATURATION:
            targetColor->saturation = map(value, 0, 127, 0, 255);
            break;
            
        case CC_COLOR_A_VALUE:
        case CC_COLOR_B_VALUE:
            targetColor->value = map(value, 0, 127, 0, 255);
            break;
            
        case CC_COLOR_A_WHITE:
        case CC_COLOR_B_WHITE:
            targetColor->white = map(value, 0, 127, 0, 255);
            break;
    }
}

DMXState::SceneEvent DMXState::handleNoteOn(byte note, byte velocity) {
    SceneEvent event;
    // Scene recall notes (36-45)
    if (note >= NOTE_SCENE_1 && note <= NOTE_SCENE_10) {
        uint8_t sceneIndex = note - NOTE_SCENE_1;
        event.triggered = true;
        event.sceneIndex = sceneIndex;
        
        if (_sceneSaveMode) {
            // Save mode: store current state
            saveCurrentAsScene(sceneIndex);
            event.saved = true;
            _sceneSaveMode = false;
            #if DEBUG_MODE
            Serial.printf("Saved scene %d\n", sceneIndex + 1);
            #endif
        } else {
            // Load mode: recall scene
            loadScene(sceneIndex);
            #if DEBUG_MODE
            Serial.printf("Loaded scene %d\n", sceneIndex + 1);
            #endif
        }
    }
    // Blackout note
    else if (note == NOTE_BLACKOUT) {
        _masterBrightness = 0;
        event.triggered = true;
        event.blackout = true;
        #if DEBUG_MODE
        Serial.println("Blackout triggered");
        #endif
    }
    return event;
}

void DMXState::handleNoteOff(byte note) {
    // Currently no action on note off
}

void DMXState::toDMXFrame(uint8_t* dmxData, uint16_t size) {
    if (size < 32) return; // Need at least 32 channels
    
    // Clear frame
    memset(dmxData, 0, size);
    
    // Pack state into DMX channels
    dmxData[DMX_CH_MASTER_BRIGHTNESS] = _masterBrightness;
    dmxData[DMX_CH_ANIMATION_MODE] = (uint8_t)_currentMode * 25; // 0-255 range, ~25 per mode
    dmxData[DMX_CH_ANIMATION_SPEED] = _animationSpeed;
    dmxData[DMX_CH_ANIMATION_CTRL] = _animationCtrl;
    dmxData[DMX_CH_STROBE_RATE] = _strobeRate;
    dmxData[DMX_CH_BLEND_MODE] = _blendMode;
    dmxData[DMX_CH_MIRROR_MODE] = _mirror;
    dmxData[DMX_CH_DIRECTION] = _direction;
    
    dmxData[DMX_CH_COLOR_A_HUE] = _colorA.hue;
    dmxData[DMX_CH_COLOR_A_SATURATION] = _colorA.saturation;
    dmxData[DMX_CH_COLOR_A_VALUE] = _colorA.value;
    dmxData[DMX_CH_COLOR_A_WHITE] = _colorA.white;
    
    dmxData[DMX_CH_COLOR_B_HUE] = _colorB.hue;
    dmxData[DMX_CH_COLOR_B_SATURATION] = _colorB.saturation;
    dmxData[DMX_CH_COLOR_B_VALUE] = _colorB.value;
    dmxData[DMX_CH_COLOR_B_WHITE] = _colorB.white;
}

LedEngineState DMXState::toLedEngineState() const {
    LedEngineState state;
    state.masterBrightness = _masterBrightness;
    state.mode = _currentMode;
    state.animationSpeed = _animationSpeed;
    state.animationCtrl = _animationCtrl;
    state.strobeRate = _strobeRate;
    state.blendMode = _blendMode;
    state.mirror = decodeMirror(_mirror);
    state.direction = decodeDirection(_direction);
    state.colorA.fromHSV(_colorA.hue, _colorA.saturation, _colorA.value, _colorA.white);
    state.colorB.fromHSV(_colorB.hue, _colorB.saturation, _colorB.value, _colorB.white);
    return state;
}

void DMXState::loadScene(uint8_t sceneIndex) {
    if (sceneIndex >= MAX_SCENES) return;
    
    ScenePreset& scene = _scenes[sceneIndex];
    _currentMode = scene.mode;
    _colorA = scene.colorA;
    _colorB = scene.colorB;
    _masterBrightness = scene.masterBrightness;
    _animationSpeed = scene.speed;
    _blendMode = scene.blendMode;
    _mirror = scene.mirror;
    _direction = scene.direction;
    _animationCtrl = scene.animationCtrl;
    _strobeRate = scene.strobeRate;
    _currentScene = sceneIndex;
}

void DMXState::saveCurrentAsScene(uint8_t sceneIndex) {
    if (sceneIndex >= MAX_SCENES) return;
    
    ScenePreset& scene = _scenes[sceneIndex];
    scene.mode = _currentMode;
    scene.colorA = _colorA;
    scene.colorB = _colorB;
    scene.masterBrightness = _masterBrightness;
    scene.speed = _animationSpeed;
    scene.blendMode = _blendMode;
    scene.mirror = _mirror;
    scene.direction = _direction;
    scene.animationCtrl = _animationCtrl;
    scene.strobeRate = _strobeRate;

    persistScenes();
}

void DMXState::initDefaultScenes() {
    // Scene 1: Red solid
    _scenes[0].mode = LedEngineLib::ANIM_SOLID;
    _scenes[0].colorA = HSVColor(0, 255, 255, 0);
    _scenes[0].colorB = HSVColor(0, 0, 0, 0);
    _scenes[0].masterBrightness = 200;
    _scenes[0].speed = 64;
    _scenes[0].blendMode = 0;
    _scenes[0].mirror = 0;
    _scenes[0].direction = 0;
    _scenes[0].animationCtrl = 0;
    _scenes[0].strobeRate = 0;
    
    // Scene 2: Blue pulse
    _scenes[1].mode = LedEngineLib::ANIM_PULSE;
    _scenes[1].colorA = HSVColor(160, 255, 255, 0);
    _scenes[1].colorB = HSVColor(0, 0, 0, 0);
    _scenes[1].masterBrightness = 255;
    _scenes[1].speed = 32;
    _scenes[1].blendMode = 0;
    _scenes[1].mirror = 0;
    _scenes[1].direction = 0;
    _scenes[1].animationCtrl = 0;
    _scenes[1].strobeRate = 0;
    
    // Scene 3: Rainbow
    _scenes[2].mode = LedEngineLib::ANIM_RAINBOW;
    _scenes[2].colorA = HSVColor(0, 255, 255, 0);
    _scenes[2].colorB = HSVColor(0, 0, 0, 0);
    _scenes[2].masterBrightness = 220;
    _scenes[2].speed = 64;
    _scenes[2].blendMode = 0;
    _scenes[2].mirror = 0;
    _scenes[2].direction = 0;
    _scenes[2].animationCtrl = 0;
    _scenes[2].strobeRate = 0;
    
    // Initialize remaining scenes with default values
    for (int i = 3; i < MAX_SCENES; i++) {
        _scenes[i].mode = LedEngineLib::ANIM_SOLID;
        _scenes[i].colorA = HSVColor(0, 0, 255, 0);
        _scenes[i].colorB = HSVColor(0, 0, 0, 0);
        _scenes[i].masterBrightness = 200;
        _scenes[i].speed = 64;
        _scenes[i].blendMode = 0;
        _scenes[i].mirror = 0;
        _scenes[i].direction = 0;
        _scenes[i].animationCtrl = 0;
        _scenes[i].strobeRate = 0;
    }
}

bool DMXState::loadScenesFromStorage() {
    if (!_prefsReady) {
        return false;
    }

    size_t expectedSize = sizeof(SceneStorageBlock);
    size_t storedSize = _preferences.getBytesLength(SCENE_STORAGE_KEY);
    if (storedSize != expectedSize) {
        return false;
    }

    SceneStorageBlock block;
    if (_preferences.getBytes(SCENE_STORAGE_KEY, &block, expectedSize) != expectedSize) {
        return false;
    }

    if (block.magic != SCENE_STORAGE_MAGIC) {
        return false;
    }

    memcpy(_scenes, block.presets, sizeof(_scenes));
    return true;
}

void DMXState::persistScenes() {
    if (!_prefsReady) {
        return;
    }

    SceneStorageBlock block;
    block.magic = SCENE_STORAGE_MAGIC;
    memcpy(block.presets, _scenes, sizeof(_scenes));
    _preferences.putBytes(SCENE_STORAGE_KEY, &block, sizeof(block));
}
