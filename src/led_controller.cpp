#include "led_controller.h"

// ColorRGBW implementation
void ColorRGBW::fromHSV(uint8_t hue, uint8_t sat, uint8_t val, uint8_t white) {
    CHSV hsv(hue, sat, val);
    CRGB rgb;
    hsv2rgb_rainbow(hsv, rgb);
    r = rgb.r;
    g = rgb.g;
    b = rgb.b;
    w = white;
}

// LEDController implementation
LEDController::LEDController() 
    : _currentMode(ANIM_SOLID)
    , _masterBrightness(LED_BRIGHTNESS)
    , _animationSpeed(64)
    , _strobeRate(0)
    , _blendMode(0)
    , _mirror(false)
    , _reverse(false)
    , _segmentSize(10)
    , _lastUpdate(0)
    , _frameCount(0)
    , _fpsTimer(0)
    , _fps(0)
    , _animationPhase(0)
    , _currentScene(-1)
{
    // Initialize with default colors
    _colorA.fromHSV(0, 255, 255, 0);      // Red
    _colorB.fromHSV(160, 255, 255, 0);    // Cyan
}

void LEDController::begin() {
    // Configure FastLED with RMT
    FastLED.addLeds<LED_TYPE, LED_DATA_PIN, LED_COLOR_ORDER>(_leds, LED_COUNT);
    FastLED.setBrightness(_masterBrightness);
    FastLED.clear();
    FastLED.show();
    
    initDefaultScenes();
    
    #if DEBUG_MODE
    Serial.println("LED Controller initialized with RMT");
    Serial.printf("Strip: %d LEDs on GPIO %d\n", LED_COUNT, LED_DATA_PIN);
    #endif
}

void LEDController::update() {
    unsigned long now = millis();
    uint32_t deltaTime = now - _lastUpdate;
    
    // Frame rate limiting
    if (deltaTime < (1000 / LED_TARGET_FPS)) {
        return;
    }
    
    _lastUpdate = now;
    
    // Update animation phase based on speed
    _animationPhase += _animationSpeed;
    
    // Render current animation
    switch (_currentMode) {
        case ANIM_SOLID:        renderSolid(); break;
        case ANIM_DUAL_SOLID:   renderDualSolid(); break;
        case ANIM_CHASE:        renderChase(); break;
        case ANIM_DASH:         renderDash(); break;
        case ANIM_STROBE:       renderStrobe(); break;
        case ANIM_PULSE:        renderPulse(); break;
        case ANIM_RAINBOW:      renderRainbow(); break;
        case ANIM_SPARKLE:      renderSparkle(); break;
        default:                renderSolid(); break;
    }
    
    // Apply effects
    if (_mirror) {
        applyMirror();
    }
    
    // Update LEDs
    FastLED.show();
    
    // Calculate FPS
    calculateFPS();
}

void LEDController::handleGlobalCC(byte controller, byte value) {
    switch (controller) {
        case CC_MASTER_BRIGHTNESS:
            _masterBrightness = map(value, 0, 127, 0, 255);
            FastLED.setBrightness(_masterBrightness);
            break;
            
        case CC_ANIMATION_SPEED:
            _animationSpeed = value;
            break;
            
        case CC_STROBE_RATE:
            _strobeRate = value;
            if (value > 0 && _currentMode != ANIM_STROBE) {
                _currentMode = ANIM_STROBE;
            }
            break;
            
        case CC_BLEND_MODE:
            _blendMode = value;
            break;
            
        case CC_MIRROR_MODE:
            _mirror = (value > 63);
            break;
            
        case CC_REVERSE:
            _reverse = (value > 63);
            break;
            
        case CC_SEGMENT_SIZE:
            _segmentSize = map(value, 0, 127, 1, LED_COUNT / 4);
            break;
            
        case CC_ANIMATION_MODE:
            if (value < ANIM_MODE_COUNT * 13) { // Map 0-127 to modes
                _currentMode = (AnimationMode)(value / 13);
            }
            break;
    }
}

void LEDController::handleColorCC(uint8_t colorBank, byte controller, byte value) {
    ColorRGBW* targetColor = (colorBank == 0) ? &_colorA : &_colorB;
    
    // Store current HSV components (approximation)
    static uint8_t hueA = 0, satA = 255, valA = 255, whiteA = 0;
    static uint8_t hueB = 160, satB = 255, valB = 255, whiteB = 0;
    
    uint8_t *hue, *sat, *val, *white;
    if (colorBank == 0) {
        hue = &hueA; sat = &satA; val = &valA; white = &whiteA;
    } else {
        hue = &hueB; sat = &satB; val = &valB; white = &whiteB;
    }
    
    // Color A: CC 20-23, Color B: CC 40-43
    uint8_t baseCC = (colorBank == 0) ? 20 : 40;
    
    switch (controller) {
        case CC_COLOR_A_HUE:
        case CC_COLOR_B_HUE:
            *hue = map(value, 0, 127, 0, 255);
            targetColor->fromHSV(*hue, *sat, *val, *white);
            break;
            
        case CC_COLOR_A_SATURATION:
        case CC_COLOR_B_SATURATION:
            *sat = map(value, 0, 127, 0, 255);
            targetColor->fromHSV(*hue, *sat, *val, *white);
            break;
            
        case CC_COLOR_A_VALUE:
        case CC_COLOR_B_VALUE:
            *val = map(value, 0, 127, 0, 255);
            targetColor->fromHSV(*hue, *sat, *val, *white);
            break;
            
        case CC_COLOR_A_WHITE:
        case CC_COLOR_B_WHITE:
            *white = map(value, 0, 127, 0, 255);
            targetColor->w = *white;
            break;
    }
}

void LEDController::handleNoteOn(byte note, byte velocity) {
    // Blackout
    if (note == NOTE_BLACKOUT) {
        FastLED.clear();
        FastLED.show();
        return;
    }
    
    // Scene triggers
    uint8_t sceneIndex = note - NOTE_SCENE_1;
    if (sceneIndex < MAX_SCENES) {
        loadScene(sceneIndex);
    }
}

void LEDController::handleNoteOff(byte note) {
    // Could be used for momentary effects
}

// ========================================
// Animation Renderers
// ========================================

void LEDController::renderSolid() {
    for (uint16_t i = 0; i < LED_COUNT; i++) {
        setPixelRGBW(i, _colorA);
    }
}

void LEDController::renderDualSolid() {
    uint16_t split = LED_COUNT / 2;
    
    // Blend mode controls the transition
    if (_blendMode == 0) {
        // Hard split
        for (uint16_t i = 0; i < split; i++) {
            setPixelRGBW(i, _colorA);
        }
        for (uint16_t i = split; i < LED_COUNT; i++) {
            setPixelRGBW(i, _colorB);
        }
    } else {
        // Gradient blend
        for (uint16_t i = 0; i < LED_COUNT; i++) {
            uint8_t blend = map(i, 0, LED_COUNT - 1, 0, 255);
            ColorRGBW mixed;
            mixed.r = lerp8by8(_colorA.r, _colorB.r, blend);
            mixed.g = lerp8by8(_colorA.g, _colorB.g, blend);
            mixed.b = lerp8by8(_colorA.b, _colorB.b, blend);
            mixed.w = lerp8by8(_colorA.w, _colorB.w, blend);
            setPixelRGBW(i, mixed);
        }
    }
}

void LEDController::renderChase() {
    uint16_t pos = (_animationPhase >> 8) % LED_COUNT;
    if (_reverse) pos = LED_COUNT - 1 - pos;
    
    // Fade all
    for (uint16_t i = 0; i < LED_COUNT; i++) {
        _leds[i].fadeToBlackBy(20);
    }
    
    // Draw chase
    for (uint8_t i = 0; i < _segmentSize && pos + i < LED_COUNT; i++) {
        setPixelRGBW(pos + i, _colorA);
    }
}

void LEDController::renderDash() {
    uint16_t offset = (_animationPhase >> 8) % (_segmentSize * 2);
    if (_reverse) offset = (_segmentSize * 2) - offset;
    
    for (uint16_t i = 0; i < LED_COUNT; i++) {
        uint16_t pos = (i + offset) % (_segmentSize * 2);
        if (pos < _segmentSize) {
            setPixelRGBW(i, _colorA);
        } else {
            setPixelRGBW(i, _colorB);
        }
    }
}

void LEDController::renderStrobe() {
    if (_strobeRate == 0) {
        renderSolid();
        return;
    }
    
    // Strobe frequency based on _strobeRate
    uint16_t period = map(_strobeRate, 1, 127, 500, 20); // ms between flashes
    bool on = (millis() % period) < (period / 10); // 10% duty cycle
    
    if (on) {
        for (uint16_t i = 0; i < LED_COUNT; i++) {
            setPixelRGBW(i, _colorA);
        }
    } else {
        FastLED.clear();
    }
}

void LEDController::renderPulse() {
    uint8_t breath = beatsin8(map(_animationSpeed, 0, 127, 10, 60), 0, 255);
    
    ColorRGBW pulsed = _colorA;
    pulsed.r = scale8(pulsed.r, breath);
    pulsed.g = scale8(pulsed.g, breath);
    pulsed.b = scale8(pulsed.b, breath);
    pulsed.w = scale8(pulsed.w, breath);
    
    for (uint16_t i = 0; i < LED_COUNT; i++) {
        setPixelRGBW(i, pulsed);
    }
}

void LEDController::renderRainbow() {
    uint8_t offset = (_animationPhase >> 8) & 0xFF;
    
    for (uint16_t i = 0; i < LED_COUNT; i++) {
        uint8_t hue = offset + (i * 255 / LED_COUNT);
        if (_reverse) hue = 255 - hue;
        ColorRGBW rainbow;
        rainbow.fromHSV(hue, 255, 255, 0);
        setPixelRGBW(i, rainbow);
    }
}

void LEDController::renderSparkle() {
    // Fade all
    for (uint16_t i = 0; i < LED_COUNT; i++) {
        _leds[i].fadeToBlackBy(10);
    }
    
    // Add random sparkles
    uint8_t numSparkles = map(_animationSpeed, 0, 127, 1, 10);
    for (uint8_t i = 0; i < numSparkles; i++) {
        uint16_t pos = random16(LED_COUNT);
        setPixelRGBW(pos, random8(2) ? _colorA : _colorB);
    }
}

// ========================================
// Utility Functions
// ========================================

void LEDController::setPixelRGBW(uint16_t index, const ColorRGBW& color) {
    if (index >= LED_COUNT) return;
    
    // For SK6812, we only set RGB in FastLED
    // White channel is handled by the SK6812 chipset automatically
    // We blend white into RGB for display
    uint8_t r = color.r + (color.w / 3);
    uint8_t g = color.g + (color.w / 3);
    uint8_t b = color.b + (color.w / 3);
    
    _leds[index] = CRGB(r, g, b);
}

void LEDController::applyMirror() {
    uint16_t half = LED_COUNT / 2;
    for (uint16_t i = 0; i < half; i++) {
        _leds[LED_COUNT - 1 - i] = _leds[i];
    }
}

void LEDController::calculateFPS() {
    _frameCount++;
    unsigned long now = millis();
    
    if (now - _fpsTimer >= 1000) {
        _fps = _frameCount;
        _frameCount = 0;
        _fpsTimer = now;
    }
}

// ========================================
// Scene Management
// ========================================

void LEDController::initDefaultScenes() {
    // Scene 1: Red solid
    _scenes[0].mode = ANIM_SOLID;
    _scenes[0].colorA.fromHSV(0, 255, 255, 0);
    _scenes[0].speed = 64;
    
    // Scene 2: Blue solid
    _scenes[1].mode = ANIM_SOLID;
    _scenes[1].colorA.fromHSV(160, 255, 255, 0);
    _scenes[1].speed = 64;
    
    // Scene 3: Rainbow chase
    _scenes[2].mode = ANIM_RAINBOW;
    _scenes[2].speed = 32;
    
    // Scene 4: Red/Blue dash
    _scenes[3].mode = ANIM_DASH;
    _scenes[3].colorA.fromHSV(0, 255, 255, 0);
    _scenes[3].colorB.fromHSV(160, 255, 255, 0);
    _scenes[3].speed = 64;
    _scenes[3].segmentSize = 20;
    
    // Scene 5: White strobe
    _scenes[4].mode = ANIM_STROBE;
    _scenes[4].colorA = ColorRGBW(255, 255, 255, 255);
    
    // Scene 6-10: User configurable (copy scene 1 as default)
    for (uint8_t i = 5; i < MAX_SCENES; i++) {
        _scenes[i] = _scenes[0];
    }
}

void LEDController::loadScene(uint8_t sceneIndex) {
    if (sceneIndex >= MAX_SCENES) return;
    
    const ScenePreset& scene = _scenes[sceneIndex];
    _currentMode = scene.mode;
    _colorA = scene.colorA;
    _colorB = scene.colorB;
    _animationSpeed = scene.speed;
    _blendMode = scene.blendMode;
    _mirror = scene.mirror;
    _reverse = scene.reverse;
    _segmentSize = scene.segmentSize;
    _currentScene = sceneIndex;
    
    #if DEBUG_MODE
    Serial.printf("Loaded scene %d\n", sceneIndex + 1);
    #endif
}

void LEDController::saveCurrentAsScene(uint8_t sceneIndex) {
    if (sceneIndex >= MAX_SCENES) return;
    
    ScenePreset& scene = _scenes[sceneIndex];
    scene.mode = _currentMode;
    scene.colorA = _colorA;
    scene.colorB = _colorB;
    scene.speed = _animationSpeed;
    scene.blendMode = _blendMode;
    scene.mirror = _mirror;
    scene.reverse = _reverse;
    scene.segmentSize = _segmentSize;
    
    #if DEBUG_MODE
    Serial.printf("Saved current state to scene %d\n", sceneIndex + 1);
    #endif
}
