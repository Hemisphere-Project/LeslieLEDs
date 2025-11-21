#include "led_controller.h"
#include "display_handler.h"

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
    : _displayHandler(nullptr)
    , _currentMode(ANIM_SOLID)
    , _masterBrightness(LED_BRIGHTNESS)
    , _animationSpeed(64)
    , _animationCtrl(0)
    , _strobeRate(0)
    , _blendMode(0)
    , _mirror(MIRROR_NONE)
    , _direction(DIR_FORWARD)
    , _waveform(WAVE_SINE)
    , _sceneSaveMode(false)
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
    // Initialize LED array to all zeros
    clearLEDs();
    
    // Configure FastLED with RMT - treat as raw 4-byte data
    FastLED.addLeds<LED_TYPE, LED_DATA_PIN>((CRGB*)_leds, LED_COUNT * 4 / 3);
    FastLED.setBrightness(77);  // Match test firmware: 30% brightness
    FastLED.show();
    
    #if DEBUG_MODE
    Serial.printf("FastLED configured: %d RGBW LEDs as %d RGB units\n", LED_COUNT, LED_COUNT * 4 / 3);
    Serial.printf("Buffer size: %d bytes\n", LED_COUNT * 4);
    Serial.printf("LED_DATA_PIN: %d\n", LED_DATA_PIN);
    #endif
    
    // Boot test sequence - ALL 300 LEDs to match test firmware
    const int testLEDs = LED_COUNT;  // Test ALL LEDs, not just 10
    
    // RED test - ALL 300 LEDs
    #if DEBUG_MODE
    Serial.println("Boot test: RED");
    #endif
    clearLEDs();
    for(int i = 0; i < testLEDs; i++) {
        _leds[i] = {0, 255, 0, 0};  // G, R, B, W
    }
    FastLED.show();
    delay(300);  // Faster
    
    // GREEN test - ALL 300 LEDs
    #if DEBUG_MODE
    Serial.println("Boot test: GREEN");
    #endif
    clearLEDs();
    for(int i = 0; i < testLEDs; i++) {
        _leds[i] = {255, 0, 0, 0};  // G, R, B, W
    }
    FastLED.show();
    delay(300);
    
    // BLUE test - ALL 300 LEDs
    #if DEBUG_MODE
    Serial.println("Boot test: BLUE");
    #endif
    clearLEDs();
    for(int i = 0; i < testLEDs; i++) {
        _leds[i] = {0, 0, 255, 0};  // G, R, B, W
    }
    FastLED.show();
    delay(300);
    
    // WHITE test (W channel) - ALL 300 LEDs
    #if DEBUG_MODE
    Serial.println("Boot test: WHITE");
    #endif
    clearLEDs();
    for(int i = 0; i < testLEDs; i++) {
        _leds[i] = {0, 0, 0, 255};  // G, R, B, W - pure white channel
    }
    FastLED.show();
    delay(300);
    
    // Clear and continue
    #if DEBUG_MODE
    Serial.println("Boot test: Complete, clearing...");
    #endif
    clearLEDs();
    FastLED.show();
    delay(200);
    
    initDefaultScenes();
    
    // Restore normal brightness
    FastLED.setBrightness(_masterBrightness);
    
    #if DEBUG_MODE
    Serial.println("LED Controller initialized");
    Serial.printf("Strip: %d LEDs on GPIO %d\n", LED_COUNT, LED_DATA_PIN);
    Serial.printf("Default mode: ANIM_SOLID, Color A (red)\n");
    #endif
}

void LEDController::setDisplayHandler(DisplayHandler* display) {
    _displayHandler = display;
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
        case ANIM_WAVEFORM:     renderWaveform(); break;
        case ANIM_PULSE:        renderPulse(); break;
        case ANIM_RAINBOW:      renderRainbow(); break;
        case ANIM_SPARKLE:      renderSparkle(); break;
        default:                renderSolid(); break;
    }
    
    // Apply effects
    if (_mirror != MIRROR_NONE) {
        applyMirror();
    }
    
    // Apply strobe overlay if enabled
    if (_strobeRate > 0) {
        applyStrobeOverlay();
    }
    
    // Update LEDs
    FastLED.show();
    
    // Calculate FPS
    calculateFPS();
}

void LEDController::handleGlobalCC(byte controller, byte value) {
    switch (controller) {
        case CC_MASTER_BRIGHTNESS:
            // CC1 uses full 0-255 range
            _masterBrightness = map(value, 0, 127, 0, 255);
            FastLED.setBrightness(_masterBrightness);
            break;
            
        case CC_ANIMATION_SPEED:
            _animationSpeed = value;
            break;
            
        case CC_ANIMATION_CTRL:
            _animationCtrl = value;
            // For ANIM_WAVEFORM: select waveform type
            if (_currentMode == ANIM_WAVEFORM) {
                if (value < 32) _waveform = WAVE_SINE;
                else if (value < 64) _waveform = WAVE_TRIANGLE;
                else if (value < 96) _waveform = WAVE_SQUARE;
                else _waveform = WAVE_SAWTOOTH;
            }
            break;
            
        case CC_STROBE_RATE:
            _strobeRate = value;
            break;
            
        case CC_BLEND_MODE:
            _blendMode = value;
            break;
            
        case CC_MIRROR_MODE:
            // 0-25=none, 26-50=full, 51-75=split2, 76-100=split3, 101-127=split4
            if (value <= 25) _mirror = MIRROR_NONE;
            else if (value <= 50) _mirror = MIRROR_FULL;
            else if (value <= 75) _mirror = MIRROR_SPLIT2;
            else if (value <= 100) _mirror = MIRROR_SPLIT3;
            else _mirror = MIRROR_SPLIT4;
            break;
            
        case CC_DIRECTION:
            // 0-25=forward, 26-50=backward, 51-75=pingpong, 76-100=random
            if (value <= 25) _direction = DIR_FORWARD;
            else if (value <= 50) _direction = DIR_BACKWARD;
            else if (value <= 75) _direction = DIR_PINGPONG;
            else if (value <= 100) _direction = DIR_RANDOM;
            break;
            
        case CC_ANIMATION_MODE:
            // 0=none, 1-9=mode0, 10-19=mode1, 20-29=mode2, etc.
            if (value == 0) {
                // No animation - blackout
                clearLEDs();
            } else if (value < 10) {
                _currentMode = ANIM_SOLID;
            } else {
                uint8_t mode = (value - 1) / 10;
                if (mode < ANIM_MODE_COUNT) {
                    _currentMode = (AnimationMode)mode;
                }
            }
            break;
            
        case CC_SCENE_SAVE_MODE:
            _sceneSaveMode = (value > 37);  // Threshold to prevent knob drift
            #if DEBUG_MODE
            Serial.printf("Scene save mode: %s (CC127=%d)\n", _sceneSaveMode ? "ON" : "OFF", value);
            #endif
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
    
    // Color A: CC 20-23, Color B: CC 30-33
    uint8_t baseCC = (colorBank == 0) ? CC_COLOR_A_HUE : CC_COLOR_B_HUE;
    
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
        clearLEDs();
        FastLED.show();
        return;
    }
    
    // Scene triggers
    uint8_t sceneIndex = note - NOTE_SCENE_1;
    if (sceneIndex < MAX_SCENES) {
        if (_sceneSaveMode) {
            // Save mode: save current state to scene
            saveCurrentAsScene(sceneIndex);
            
            // Show notification on display
            if (_displayHandler) {
                _displayHandler->showSceneNotification(sceneIndex, true);
            }
            
            // Flash LEDs briefly to confirm save
            for(int i = 0; i < LED_COUNT; i++) {
                _leds[i] = {255, 0, 0, 0};  // Green: G, R, B, W
            }
            FastLED.show();
            delay(100);
            clearLEDs();
            FastLED.show();
            delay(50);
        } else {
            // Normal mode: load scene
            loadScene(sceneIndex);
            
            // Show notification on display
            if (_displayHandler) {
                _displayHandler->showSceneNotification(sceneIndex, false);
            }
        }
    }
}

void LEDController::handleNoteOff(byte note) {
    // Could be used for momentary effects
}

// ========================================
// Animation Renderers
// ========================================

void LEDController::renderSolid() {
    static bool first = true;
    if (first) {
        #if DEBUG_MODE
        Serial.printf("renderSolid: colorA R=%d G=%d B=%d W=%d\n", 
                      _colorA.r, _colorA.g, _colorA.b, _colorA.w);
        #endif
        first = false;
    }
    
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
    uint16_t segmentSize = map(_animationCtrl, 0, 127, 1, LED_COUNT / 4);
    uint16_t pos = (_animationPhase >> 8) % LED_COUNT;
    
    // Apply direction
    switch (_direction) {
        case DIR_BACKWARD:
            pos = LED_COUNT - 1 - pos;
            break;
        case DIR_PINGPONG:
            // Snap back to start after reaching end
            if (((_animationPhase >> 8) / LED_COUNT) % 2 == 1) {
                pos = 0; // Snap back
            }
            break;
        case DIR_RANDOM:
            pos = random16(LED_COUNT);
            break;
        case DIR_FORWARD:
        default:
            break;
    }
    
    // Fade all
    for (uint16_t i = 0; i < LED_COUNT; i++) {
        fadePixel(i, 20);
    }
    
    // Draw chase
    for (uint8_t i = 0; i < segmentSize && pos + i < LED_COUNT; i++) {
        setPixelRGBW(pos + i, _colorA);
    }
}

void LEDController::renderDash() {
    uint16_t segmentSize = map(_animationCtrl, 0, 127, 1, LED_COUNT / 4);
    uint16_t offset = (_animationPhase >> 8) % (segmentSize * 2);
    
    // Apply direction
    switch (_direction) {
        case DIR_BACKWARD:
            offset = (segmentSize * 2) - offset;
            break;
        case DIR_PINGPONG:
            if (((_animationPhase >> 8) / (segmentSize * 2)) % 2 == 1) {
                offset = 0; // Snap back
            }
            break;
        case DIR_RANDOM:
            offset = random16(segmentSize * 2);
            break;
        case DIR_FORWARD:
        default:
            break;
    }
    
    for (uint16_t i = 0; i < LED_COUNT; i++) {
        uint16_t pos = (i + offset) % (segmentSize * 2);
        if (pos < segmentSize) {
            setPixelRGBW(i, _colorA);
        } else {
            setPixelRGBW(i, _colorB);
        }
    }
}

void LEDController::renderWaveform() {
    uint8_t phase = (_animationPhase >> 8) & 0xFF;
    
    // Calculate waveform value based on type (selected via CC3)
    uint8_t waveValue = 0;
    switch (_waveform) {
        case WAVE_SINE:
            waveValue = sin8(phase);
            break;
        case WAVE_TRIANGLE:
            waveValue = (phase < 128) ? (phase * 2) : (255 - (phase - 128) * 2);
            break;
        case WAVE_SQUARE:
            waveValue = (phase < 128) ? 255 : 0;
            break;
        case WAVE_SAWTOOTH:
            waveValue = phase;
            break;
    }
    
    // Apply waveform to color
    ColorRGBW waved = _colorA;
    waved.r = scale8(waved.r, waveValue);
    waved.g = scale8(waved.g, waveValue);
    waved.b = scale8(waved.b, waveValue);
    waved.w = scale8(waved.w, waveValue);
    
    for (uint16_t i = 0; i < LED_COUNT; i++) {
        setPixelRGBW(i, waved);
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
        
        // Apply direction
        switch (_direction) {
            case DIR_BACKWARD:
                hue = 255 - hue;
                break;
            case DIR_PINGPONG:
                if (((_animationPhase >> 16) % 2) == 1) {
                    hue = offset; // Snap back
                }
                break;
            case DIR_RANDOM:
                hue = random8();
                break;
            case DIR_FORWARD:
            default:
                break;
        }
        
        ColorRGBW rainbow;
        rainbow.fromHSV(hue, 255, 255, 0);
        setPixelRGBW(i, rainbow);
    }
}

void LEDController::renderSparkle() {
    // Fade all
    for (uint16_t i = 0; i < LED_COUNT; i++) {
        fadePixel(i, 10);
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
    
    // Set RGBW pixel
    // ColorRGBW has {r, g, b, w} member order
    // CRGBW buffer has {g, r, b, w} member order (GRB for SK6812)
    // So we assign: buffer.g ← color.g, buffer.r ← color.r, etc.
    _leds[index].g = color.g;
    _leds[index].r = color.r;
    _leds[index].b = color.b;
    _leds[index].w = color.w;
}

void LEDController::clearLEDs() {
    for(int i = 0; i < LED_COUNT; i++) {
        _leds[i] = {0, 0, 0, 0};
    }
}

void LEDController::fadePixel(uint16_t index, uint8_t amount) {
    if (index >= LED_COUNT) return;
    _leds[index].r = scale8(_leds[index].r, 255 - amount);
    _leds[index].g = scale8(_leds[index].g, 255 - amount);
    _leds[index].b = scale8(_leds[index].b, 255 - amount);
    _leds[index].w = scale8(_leds[index].w, 255 - amount);
}

void LEDController::applyMirror() {
    switch (_mirror) {
        case MIRROR_NONE:
            return;
            
        case MIRROR_FULL:
            {
                uint16_t half = LED_COUNT / 2;
                for (uint16_t i = 0; i < half; i++) {
                    _leds[LED_COUNT - 1 - i] = _leds[i];
                }
            }
            break;
            
        case MIRROR_SPLIT2:
            {
                // Quarter segments with even ones reversed
                uint16_t quarter = LED_COUNT / 4;
                for (uint16_t seg = 0; seg < 4; seg++) {
                    uint16_t start = seg * quarter;
                    if (seg % 2 == 1) {
                        // Reverse even segments
                        for (uint16_t i = 0; i < quarter / 2; i++) {
                            CRGBW temp = _leds[start + i];
                            _leds[start + i] = _leds[start + quarter - 1 - i];
                            _leds[start + quarter - 1 - i] = temp;
                        }
                    }
                }
            }
            break;
            
        case MIRROR_SPLIT3:
            {
                // Sixth segments with even ones reversed
                uint16_t sixth = LED_COUNT / 6;
                for (uint16_t seg = 0; seg < 6; seg++) {
                    uint16_t start = seg * sixth;
                    if (seg % 2 == 1) {
                        // Reverse even segments
                        for (uint16_t i = 0; i < sixth / 2; i++) {
                            CRGBW temp = _leds[start + i];
                            _leds[start + i] = _leds[start + sixth - 1 - i];
                            _leds[start + sixth - 1 - i] = temp;
                        }
                    }
                }
            }
            break;
            
        case MIRROR_SPLIT4:
            {
                // Eighth segments with even ones reversed
                uint16_t eighth = LED_COUNT / 8;
                for (uint16_t seg = 0; seg < 8; seg++) {
                    uint16_t start = seg * eighth;
                    if (seg % 2 == 1) {
                        // Reverse even segments
                        for (uint16_t i = 0; i < eighth / 2; i++) {
                            CRGBW temp = _leds[start + i];
                            _leds[start + i] = _leds[start + eighth - 1 - i];
                            _leds[start + eighth - 1 - i] = temp;
                        }
                    }
                }
            }
            break;
    }
}

void LEDController::applyStrobeOverlay() {
    if (_strobeRate == 0) return;
    
    // Strobe frequency based on _strobeRate (CC4)
    uint16_t period = map(_strobeRate, 1, 127, 500, 20); // ms between flashes
    uint16_t timeInPeriod = millis() % period;
    
    // PWM dimming - diminishing duty cycle
    uint8_t dimFactor = 255;
    if (timeInPeriod < (period / 10)) {
        // Flash on for 10% of period
        dimFactor = 255;
    } else if (timeInPeriod < (period / 5)) {
        // Fade out for next 10%
        dimFactor = map(timeInPeriod, period / 10, period / 5, 255, 0);
    } else {
        // Off for remaining time
        dimFactor = 0;
    }
    
    // Apply dimming to all LEDs
    for (uint16_t i = 0; i < LED_COUNT; i++) {
        _leds[i].r = scale8(_leds[i].r, dimFactor);
        _leds[i].g = scale8(_leds[i].g, dimFactor);
        _leds[i].b = scale8(_leds[i].b, dimFactor);
        _leds[i].w = scale8(_leds[i].w, dimFactor);
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
    _scenes[0].mirror = MIRROR_NONE;
    _scenes[0].direction = DIR_FORWARD;
    
    // Scene 2: Blue solid
    _scenes[1].mode = ANIM_SOLID;
    _scenes[1].colorA.fromHSV(160, 255, 255, 0);
    _scenes[1].speed = 64;
    _scenes[1].mirror = MIRROR_NONE;
    _scenes[1].direction = DIR_FORWARD;
    
    // Scene 3: Rainbow chase
    _scenes[2].mode = ANIM_RAINBOW;
    _scenes[2].speed = 32;
    _scenes[2].mirror = MIRROR_NONE;
    _scenes[2].direction = DIR_FORWARD;
    
    // Scene 4: Red/Blue dash
    _scenes[3].mode = ANIM_DASH;
    _scenes[3].colorA.fromHSV(0, 255, 255, 0);
    _scenes[3].colorB.fromHSV(160, 255, 255, 0);
    _scenes[3].speed = 64;
    _scenes[3].animationCtrl = 40;  // Segment size via CC3
    _scenes[3].mirror = MIRROR_NONE;
    _scenes[3].direction = DIR_FORWARD;
    
    // Scene 5: White waveform (sine wave)
    _scenes[4].mode = ANIM_WAVEFORM;
    _scenes[4].colorA = ColorRGBW(255, 255, 255, 255);
    _scenes[4].speed = 32;
    _scenes[4].animationCtrl = 0;  // Sine wave via CC3
    _scenes[4].mirror = MIRROR_NONE;
    _scenes[4].direction = DIR_FORWARD;
    
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
    _direction = scene.direction;
    _animationCtrl = scene.animationCtrl;
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
    scene.direction = _direction;
    scene.animationCtrl = _animationCtrl;
    
    #if DEBUG_MODE
    Serial.printf("Saved current state to scene %d\n", sceneIndex + 1);
    #endif
}
