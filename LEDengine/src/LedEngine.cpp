#include "LedEngine.h"

namespace LedEngineLib {

namespace {
uint8_t clampByte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<uint8_t>(value);
}
}

void ColorRGBW::fromHSV(uint8_t hue, uint8_t sat, uint8_t val, uint8_t white) {
    CHSV hsv(hue, sat, val);
    CRGB rgb;
    hsv2rgb_rainbow(hsv, rgb);
    r = rgb.r;
    g = rgb.g;
    b = rgb.b;
    w = white;
}

LedEngine::LedEngine(const LedEngineConfig& config)
    : _config(config),
      _state(),
      _leds(nullptr),
      _previewBuffer(nullptr),
      _initialized(false),
      _animationPhase(0),
      _lastUpdateClock(0),
      _frameIntervalMs(config.targetFPS == 0 ? 16 : 1000 / config.targetFPS),
      _frameCount(0),
      _fpsTimer(0),
      _fps(0) {
    _state.masterBrightness = _config.defaultBrightness;
    _state.colorA = ColorRGBW(0, 0, 0, 0);
    _state.colorB = ColorRGBW(0, 0, 0, 0);
}

LedEngine::~LedEngine() {
    delete[] _leds;
    delete[] _previewBuffer;
}

bool LedEngine::begin() {
    if (_initialized) {
        return true;
    }

    if (_config.ledCount == 0) {
        return false;
    }

    _leds = new CRGBW[_config.ledCount];
    _previewBuffer = new CRGB[_config.ledCount];
    clearLEDs();

    const uint16_t rgbSlots = _config.enableRGBW ? (_config.ledCount * 4 / 3) : _config.ledCount;
    FastLED.addLeds<LEDENGINE_CHIPSET, LEDENGINE_DATA_PIN, LEDENGINE_COLOR_ORDER>(reinterpret_cast<CRGB*>(_leds), rgbSlots);
    FastLED.setBrightness(_state.masterBrightness);
    FastLED.clear();
    FastLED.show();

    _initialized = true;
    _lastUpdateClock = 0;
    _animationPhase = 0;
    _frameCount = 0;
    _fpsTimer = millis();
    return true;
}

void LedEngine::update(uint32_t clockMillis, const LedEngineState& state) {
    if (!_initialized) {
        return;
    }

    _state = state;
    FastLED.setBrightness(_state.masterBrightness);

    if (_frameIntervalMs == 0) {
        _frameIntervalMs = 16;
    }

    uint32_t elapsed = (_lastUpdateClock == 0) ? _frameIntervalMs : (clockMillis - _lastUpdateClock);
    if (_lastUpdateClock != 0 && elapsed < _frameIntervalMs) {
        return;
    }
    if (elapsed == 0) {
        elapsed = 1;
    }

    _animationPhase += static_cast<uint32_t>(_state.animationSpeed) * elapsed;
    _lastUpdateClock = clockMillis;
    renderFrame(clockMillis);
}

void LedEngine::renderFrame(uint32_t clockMillis) {
    switch (_state.mode) {
        case ANIM_SOLID:
            renderSolid();
            break;
        case ANIM_DUAL_SOLID:
            renderDualSolid();
            break;
        case ANIM_CHASE:
            renderChase();
            break;
        case ANIM_DASH:
            renderDash();
            break;
        case ANIM_WAVEFORM:
            renderWaveform();
            break;
        case ANIM_PULSE:
            renderPulse(clockMillis);
            break;
        case ANIM_RAINBOW:
            renderRainbow();
            break;
        case ANIM_SPARKLE:
            renderSparkle();
            break;
        default:
            renderSolid();
            break;
    }

    applyMirror();
    applyStrobeOverlay(clockMillis);
}

void LedEngine::show() {
    if (!_initialized) {
        return;
    }
    FastLED.show();
    calculateFPS();
}

const CRGB* LedEngine::getPreviewPixels() const {
    if (!_initialized || !_previewBuffer) {
        return nullptr;
    }

    for (uint16_t i = 0; i < _config.ledCount; ++i) {
        _previewBuffer[i].r = _leds[i].r;
        _previewBuffer[i].g = _leds[i].g;
        _previewBuffer[i].b = _leds[i].b;
    }
    return _previewBuffer;
}

void LedEngine::renderSolid() {
    for (uint16_t i = 0; i < _config.ledCount; ++i) {
        setPixelRGBW(i, _state.colorA);
    }
}

void LedEngine::renderDualSolid() {
    uint16_t split = _config.ledCount / 2;

    if (_state.blendMode == 0) {
        for (uint16_t i = 0; i < split; ++i) {
            setPixelRGBW(i, _state.colorA);
        }
        for (uint16_t i = split; i < _config.ledCount; ++i) {
            setPixelRGBW(i, _state.colorB);
        }
    } else {
        uint16_t lastIndex = (_config.ledCount > 1) ? (_config.ledCount - 1) : 1;
        for (uint16_t i = 0; i < _config.ledCount; ++i) {
            uint8_t blend = map(i, 0, lastIndex, 0, 255);
            ColorRGBW mixed;
            mixed.r = lerp8by8(_state.colorA.r, _state.colorB.r, blend);
            mixed.g = lerp8by8(_state.colorA.g, _state.colorB.g, blend);
            mixed.b = lerp8by8(_state.colorA.b, _state.colorB.b, blend);
            mixed.w = lerp8by8(_state.colorA.w, _state.colorB.w, blend);
            setPixelRGBW(i, mixed);
        }
    }
}

void LedEngine::renderChase() {
    uint16_t maxSegment = _config.ledCount / 4;
    if (maxSegment == 0) {
        maxSegment = 1;
    }
    uint16_t segmentSize = map(_state.animationCtrl, 0, 255, 1, maxSegment);
    if (segmentSize == 0) {
        segmentSize = 1;
    }
    uint16_t pos = ((_animationPhase >> 8) % _config.ledCount);

    switch (_state.direction) {
        case DIR_BACKWARD:
            pos = _config.ledCount - 1 - pos;
            break;
        case DIR_PINGPONG:
            if (((_animationPhase >> 8) / _config.ledCount) % 2 == 1) {
                pos = 0;
            }
            break;
        case DIR_RANDOM:
            pos = random16(_config.ledCount);
            break;
        case DIR_FORWARD:
        default:
            break;
    }

    for (uint16_t i = 0; i < _config.ledCount; ++i) {
        fadePixel(i, 20);
    }

    for (uint16_t i = 0; i < segmentSize && pos + i < _config.ledCount; ++i) {
        setPixelRGBW(pos + i, _state.colorA);
    }
}

void LedEngine::renderDash() {
    uint16_t maxSegment = _config.ledCount / 4;
    if (maxSegment == 0) {
        maxSegment = 1;
    }
    uint16_t segmentSize = map(_state.animationCtrl, 0, 255, 1, maxSegment);
    if (segmentSize == 0) {
        segmentSize = 1;
    }
    uint16_t offset = ((_animationPhase >> 8) % (segmentSize * 2));

    switch (_state.direction) {
        case DIR_BACKWARD:
            offset = (segmentSize * 2) - offset;
            break;
        case DIR_PINGPONG:
            if (((_animationPhase >> 8) / (segmentSize * 2)) % 2 == 1) {
                offset = 0;
            }
            break;
        case DIR_RANDOM:
            offset = random16(segmentSize * 2);
            break;
        case DIR_FORWARD:
        default:
            break;
    }

    for (uint16_t i = 0; i < _config.ledCount; ++i) {
        uint16_t pos = (i + offset) % (segmentSize * 2);
        if (pos < segmentSize) {
            setPixelRGBW(i, _state.colorA);
        } else {
            setPixelRGBW(i, _state.colorB);
        }
    }
}

WaveformType LedEngine::currentWaveform() const {
    if (_state.animationCtrl < 64) {
        return WAVE_SINE;
    } else if (_state.animationCtrl < 128) {
        return WAVE_TRIANGLE;
    } else if (_state.animationCtrl < 192) {
        return WAVE_SQUARE;
    }
    return WAVE_SAWTOOTH;
}

void LedEngine::renderWaveform() {
    uint8_t phase = (_animationPhase >> 8) & 0xFF;
    uint8_t waveValue = 0;

    switch (currentWaveform()) {
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

    ColorRGBW waved = _state.colorA;
    waved.r = scale8(waved.r, waveValue);
    waved.g = scale8(waved.g, waveValue);
    waved.b = scale8(waved.b, waveValue);
    waved.w = scale8(waved.w, waveValue);

    for (uint16_t i = 0; i < _config.ledCount; ++i) {
        setPixelRGBW(i, waved);
    }
}

void LedEngine::renderPulse(uint32_t clockMillis) {
    uint8_t bpm = map(_state.animationSpeed, 0, 255, 10, 60);
    uint8_t breath = beatsin8(bpm, 0, 255, clockMillis);

    ColorRGBW pulsed = _state.colorA;
    pulsed.r = scale8(pulsed.r, breath);
    pulsed.g = scale8(pulsed.g, breath);
    pulsed.b = scale8(pulsed.b, breath);
    pulsed.w = scale8(pulsed.w, breath);

    for (uint16_t i = 0; i < _config.ledCount; ++i) {
        setPixelRGBW(i, pulsed);
    }
}

void LedEngine::renderRainbow() {
    uint8_t offset = (_animationPhase >> 8) & 0xFF;

    for (uint16_t i = 0; i < _config.ledCount; ++i) {
        uint8_t hue = offset + (i * 255 / _config.ledCount);

        switch (_state.direction) {
            case DIR_BACKWARD:
                hue = 255 - hue;
                break;
            case DIR_PINGPONG:
                if (((_animationPhase >> 16) % 2) == 1) {
                    hue = offset;
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

void LedEngine::renderSparkle() {
    for (uint16_t i = 0; i < _config.ledCount; ++i) {
        fadePixel(i, 10);
    }

    uint8_t numSparkles = map(_state.animationSpeed, 0, 255, 1, 10);
    for (uint8_t i = 0; i < numSparkles; ++i) {
        uint16_t pos = random16(_config.ledCount);
        setPixelRGBW(pos, random8(2) ? _state.colorA : _state.colorB);
    }
}

void LedEngine::setPixelRGBW(uint16_t index, const ColorRGBW& color) {
    if (index >= _config.ledCount) {
        return;
    }
    _leds[index].g = color.g;
    _leds[index].r = color.r;
    _leds[index].b = color.b;
    _leds[index].w = color.w;
}

void LedEngine::clearLEDs() {
    if (!_leds) {
        return;
    }
    for (uint16_t i = 0; i < _config.ledCount; ++i) {
        _leds[i] = {0, 0, 0, 0};
    }
}

void LedEngine::fadePixel(uint16_t index, uint8_t amount) {
    if (index >= _config.ledCount) {
        return;
    }
    _leds[index].r = scale8(_leds[index].r, 255 - amount);
    _leds[index].g = scale8(_leds[index].g, 255 - amount);
    _leds[index].b = scale8(_leds[index].b, 255 - amount);
    _leds[index].w = scale8(_leds[index].w, 255 - amount);
}

void LedEngine::applyMirror() {
    switch (_state.mirror) {
        case MIRROR_NONE:
            return;
        case MIRROR_FULL: {
            uint16_t half = _config.ledCount / 2;
            for (uint16_t i = 0; i < half; ++i) {
                _leds[_config.ledCount - 1 - i] = _leds[i];
            }
            break;
        }
        case MIRROR_SPLIT2: {
            uint16_t quarter = _config.ledCount / 4;
            if (quarter == 0) {
                return;
            }
            for (uint16_t seg = 0; seg < 4; ++seg) {
                uint16_t start = seg * quarter;
                if (seg % 2 == 1) {
                    for (uint16_t i = 0; i < quarter / 2; ++i) {
                        CRGBW temp = _leds[start + i];
                        _leds[start + i] = _leds[start + quarter - 1 - i];
                        _leds[start + quarter - 1 - i] = temp;
                    }
                }
            }
            break;
        }
        case MIRROR_SPLIT3: {
            uint16_t sixth = _config.ledCount / 6;
            if (sixth == 0) {
                return;
            }
            for (uint16_t seg = 0; seg < 6; ++seg) {
                uint16_t start = seg * sixth;
                if (seg % 2 == 1) {
                    for (uint16_t i = 0; i < sixth / 2; ++i) {
                        CRGBW temp = _leds[start + i];
                        _leds[start + i] = _leds[start + sixth - 1 - i];
                        _leds[start + sixth - 1 - i] = temp;
                    }
                }
            }
            break;
        }
        case MIRROR_SPLIT4: {
            uint16_t eighth = _config.ledCount / 8;
            if (eighth == 0) {
                return;
            }
            for (uint16_t seg = 0; seg < 8; ++seg) {
                uint16_t start = seg * eighth;
                if (seg % 2 == 1) {
                    for (uint16_t i = 0; i < eighth / 2; ++i) {
                        CRGBW temp = _leds[start + i];
                        _leds[start + i] = _leds[start + eighth - 1 - i];
                        _leds[start + eighth - 1 - i] = temp;
                    }
                }
            }
            break;
        }
    }
}

void LedEngine::applyStrobeOverlay(uint32_t clockMillis) {
    if (_state.strobeRate == 0) {
        return;
    }

    uint16_t period = map(_state.strobeRate, 1, 255, 500, 20);
    if (period == 0) {
        period = 20;
    }
    uint16_t timeInPeriod = clockMillis % period;

    uint8_t dimFactor = 0;
    if (timeInPeriod < (period / 10)) {
        dimFactor = 255;
    } else if (timeInPeriod < (period / 5)) {
        dimFactor = map(timeInPeriod, period / 10, period / 5, 255, 0);
    } else {
        dimFactor = 0;
    }

    for (uint16_t i = 0; i < _config.ledCount; ++i) {
        _leds[i].r = scale8(_leds[i].r, dimFactor);
        _leds[i].g = scale8(_leds[i].g, dimFactor);
        _leds[i].b = scale8(_leds[i].b, dimFactor);
        _leds[i].w = scale8(_leds[i].w, dimFactor);
    }
}

void LedEngine::calculateFPS() {
    _frameCount++;
    unsigned long now = millis();
    if (now - _fpsTimer >= 1000) {
        _fps = clampByte(_frameCount);
        _frameCount = 0;
        _fpsTimer = now;
    }
}

} // namespace LedEngineLib
