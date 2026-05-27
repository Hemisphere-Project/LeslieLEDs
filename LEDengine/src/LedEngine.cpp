#include "LedEngine.h"

#include <cmath>
#include <cstdlib>
#include <cstring>


#if defined(ARDUINO_ARCH_ESP32)
#include <esp_random.h>
#include <esp_task_wdt.h>
#endif

namespace LedEngineLib {

namespace {

constexpr float LEDENGINE_TWO_PI = 6.28318530718f;

bool g_rmtInitialized = false;

uint32_t nextRandom32() {
#if defined(ARDUINO_ARCH_ESP32)
    return esp_random();
#else
    return (static_cast<uint32_t>(rand()) << 16) ^ static_cast<uint32_t>(rand());
#endif
}

uint8_t sin8(uint8_t theta) {
    float angle = (static_cast<float>(theta) / 255.0f) * LEDENGINE_TWO_PI;
    float s = sinf(angle);
    int value = static_cast<int>((s + 1.0f) * 127.5f + 0.5f);
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    return static_cast<uint8_t>(value);
}

uint8_t beatsin8(uint8_t bpm, uint8_t low, uint8_t high, uint32_t timeMs) {
    if (bpm == 0 || high <= low) {
        return low;
    }
    uint64_t scaledTime = static_cast<uint64_t>(timeMs) * bpm;
    uint32_t beat = (scaledTime * 256ULL) / 60000ULL;
    uint8_t angle = static_cast<uint8_t>(beat & 0xFF);
    uint8_t sine = sin8(angle);
    uint8_t range = high - low;
    uint8_t scaled = scale8(sine, range);
    return low + scaled;
}

uint16_t random16(uint16_t maxValue) {
    if (maxValue == 0) {
        return 0;
    }
    return static_cast<uint16_t>(nextRandom32() % maxValue);
}

uint8_t random8() {
    return static_cast<uint8_t>(nextRandom32() & 0xFF);
}

uint8_t random8(uint8_t maxExclusive) {
    if (maxExclusive == 0) {
        return 0;
    }
    return static_cast<uint8_t>(nextRandom32() % maxExclusive);
}

void hsvToRgb(uint8_t hue, uint8_t sat, uint8_t val, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (sat == 0) {
        r = g = b = val;
        return;
    }

    uint8_t region = hue / 43;
    uint8_t remainder = (hue - (region * 43)) * 6;

    uint16_t p = (static_cast<uint16_t>(val) * (255 - sat)) >> 8;
    uint16_t q = (static_cast<uint16_t>(val) * (255 - ((static_cast<uint16_t>(sat) * remainder) >> 8))) >> 8;
    uint16_t t = (static_cast<uint16_t>(val) * (255 - ((static_cast<uint16_t>(sat) * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:
            r = val;
            g = static_cast<uint8_t>(t);
            b = static_cast<uint8_t>(p);
            break;
        case 1:
            r = static_cast<uint8_t>(q);
            g = val;
            b = static_cast<uint8_t>(p);
            break;
        case 2:
            r = static_cast<uint8_t>(p);
            g = val;
            b = static_cast<uint8_t>(t);
            break;
        case 3:
            r = static_cast<uint8_t>(p);
            g = static_cast<uint8_t>(q);
            b = val;
            break;
        case 4:
            r = static_cast<uint8_t>(t);
            g = static_cast<uint8_t>(p);
            b = val;
            break;
        default:
            r = val;
            g = static_cast<uint8_t>(p);
            b = static_cast<uint8_t>(q);
            break;
    }
}

uint8_t clampByte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<uint8_t>(value);
}

uint16_t pingPongIndex(uint32_t value, uint16_t limit) {
    if (limit <= 1) {
        return 0;
    }
    uint32_t span = static_cast<uint32_t>(limit - 1);
    uint32_t period = span * 2;
    if (period == 0) {
        return 0;
    }
    uint32_t phase = value % period;
    if (phase <= span) {
        return static_cast<uint16_t>(phase);
    }
    return static_cast<uint16_t>(period - phase);
}

} // namespace

void ColorRGBW::fromHSV(uint8_t hue, uint8_t sat, uint8_t val, uint8_t white) {
    hsvToRgb(hue, sat, val, r, g, b);
    w = white;
}

LedEngine::LedEngine(const LedEngineConfig& config)
    : _config(config),
      _state(),
      _renderBuffer(nullptr),
      _hwBuffer(nullptr),
    _strand(nullptr),
      _previewBuffer(nullptr),
      _initialized(false),
    _animationPhase(0),
    _lastUpdateClock(0),
    _lastLocalMillis(0),
      _frameIntervalMs(config.targetFPS == 0 ? 16 : 1000 / config.targetFPS),
      _frameCount(0),
      _fpsTimer(0),
      _fps(0),
      _renderTaskHandle(nullptr),
      _stateMutex(nullptr),
    _bufferMutex(nullptr),
    _stateDirty(false),
    _pendingClockMillis(0),
    _pendingLocalMillis(0) {
    _state.masterBrightness = _config.defaultBrightness;
    _state.colorA = ColorRGBW(0, 0, 0, 0);
    _state.colorB = ColorRGBW(0, 0, 0, 0);
    _pendingState = _state;
}

LedEngine::~LedEngine() {
#if defined(ARDUINO_ARCH_ESP32)
    if (_renderTaskHandle) {
        vTaskDelete(_renderTaskHandle);
        _renderTaskHandle = nullptr;
    }
    if (_stateMutex) {
        vSemaphoreDelete(_stateMutex);
        _stateMutex = nullptr;
    }
    if (_bufferMutex) {
        vSemaphoreDelete(_bufferMutex);
        _bufferMutex = nullptr;
    }
#endif
    delete[] _renderBuffer;
    _renderBuffer = nullptr;
    _hwBuffer = nullptr;
    delete[] _previewBuffer;
    _previewBuffer = nullptr;
    if (_strand) {
        LibStrip::resetStrand(_strand);
        _strand = nullptr;
    }
}

bool LedEngine::begin() {
    if (_initialized) {
        return true;
    }

    if (_config.ledCount == 0) {
        return false;
    }

    if (!_previewBuffer) {
        _previewBuffer = new CRGB[_config.ledCount];
    }

    if (!g_rmtInitialized) {
        if (LibStrip::init() != 0) {
            return false;
        }
        g_rmtInitialized = true;
    }

    led_types ledType = _config.enableRGBW ? LED_SK6812W_V4 : LED_SK6812_V1;
    if (_config.ledTypeOverride >= 0) {
        ledType = static_cast<led_types>(_config.ledTypeOverride);
    }

    strand_t strand = {};
    strand.rmtChannel = _config.rmtChannel;
    strand.gpioNum = _config.dataPin;
    strand.ledType = ledType;
    strand.brightLimit = 255;
    strand.numPixels = _config.ledCount;
    strand.pixels = nullptr;
    strand._stateVars = nullptr;

    _strand = LibStrip::addStrand(strand);
    if (!_strand) {
        return false;
    }

    _hwBuffer = reinterpret_cast<CRGBW*>(_strand->pixels);
    if (!_renderBuffer) {
        _renderBuffer = new CRGBW[_config.ledCount];
    }
    clearLEDs();

#if defined(ARDUINO_ARCH_ESP32)
    if (!_stateMutex) {
        _stateMutex = xSemaphoreCreateMutex();
    }
    if (!_bufferMutex) {
        _bufferMutex = xSemaphoreCreateMutex();
    }
#endif

    if (_hwBuffer && _renderBuffer) {
        memcpy(_hwBuffer, _renderBuffer, sizeof(CRGBW) * _config.ledCount);
        LibStrip::updatePixels(_strand);
    }

#if defined(ARDUINO_ARCH_ESP32)
    if (!_renderTaskHandle) {
        constexpr UBaseType_t kRenderPriority = configMAX_PRIORITIES - 2;
        const BaseType_t created = xTaskCreatePinnedToCore(
            LedEngine::renderTaskTrampoline,
            "LEDRender",
            4096,
            this,
            kRenderPriority,
            &_renderTaskHandle,
            1);
        if (created != pdPASS) {
            _renderTaskHandle = nullptr;
            return false;
        }
    }
#endif

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

#if defined(ARDUINO_ARCH_ESP32)
    if (_stateMutex && xSemaphoreTake(_stateMutex, portMAX_DELAY) == pdTRUE) {
        _pendingState = state;
        _pendingClockMillis = clockMillis;
        _pendingLocalMillis = millis();
        _stateDirty = true;
        xSemaphoreGive(_stateMutex);
    }
#else
    _state = state;
    _pendingState = state;
    _stateDirty = true;
    _pendingClockMillis = clockMillis;
    _pendingLocalMillis = millis();
    serviceRenderTick();
#endif
}

void LedEngine::renderTaskTrampoline(void* param) {
#if defined(ARDUINO_ARCH_ESP32)
    static_cast<LedEngine*>(param)->renderTaskLoop();
#endif
}

void LedEngine::renderTaskLoop() {
#if defined(ARDUINO_ARCH_ESP32)
    // Subscribe the render task to the same Task WDT the main loop uses.
    // If the render task ever deadlocks (mutex held forever, RMT driver
    // wedge, anything that stops serviceRenderTick from completing), the
    // chip self-resets instead of leaving the strip stuck on the last
    // frame with no operator-visible indication. Returning ESP_ERR_INVALID_STATE
    // here means the project hasn't configured a Task WDT yet — in that
    // case we silently skip subscription.
    bool wdtSubscribed = (esp_task_wdt_add(nullptr) == ESP_OK);

    TickType_t lastWake = xTaskGetTickCount();
    while (true) {
        if (wdtSubscribed) {
            esp_task_wdt_reset();
        }
        serviceRenderTick();
        uint32_t intervalMs = _frameIntervalMs == 0 ? 16 : _frameIntervalMs;
        TickType_t frameTicks = pdMS_TO_TICKS(intervalMs);
        if (frameTicks == 0) {
            frameTicks = 1;
        }
        vTaskDelayUntil(&lastWake, frameTicks);
    }
#endif
}

void LedEngine::serviceRenderTick() {
    if (!_initialized || !_strand || !_renderBuffer) {
        return;
    }

    uint32_t localMillis = millis();
    uint32_t clockMillis = 0;
    uint32_t prevClock = _lastUpdateClock;
    uint32_t prevLocal = _lastLocalMillis;
    uint32_t anchorClock = prevClock;
    uint32_t anchorLocal = prevLocal;

#if defined(ARDUINO_ARCH_ESP32)
    if (_stateMutex && xSemaphoreTake(_stateMutex, portMAX_DELAY) == pdTRUE) {
        if (_stateDirty) {
            _state = _pendingState;
            _stateDirty = false;
        }
        if (_pendingClockMillis != 0) {
            uint32_t pendingClock = _pendingClockMillis;
            uint32_t pendingLocal = _pendingLocalMillis;
            if (pendingLocal == 0) {
                pendingLocal = localMillis;
            }
            _pendingClockMillis = 0;
            _pendingLocalMillis = 0;
            uint32_t delta = localMillis - pendingLocal;
            clockMillis = pendingClock + delta;
            anchorClock = clockMillis;
            anchorLocal = localMillis;
        }
        xSemaphoreGive(_stateMutex);
    }
#else
    if (_stateDirty) {
        _state = _pendingState;
        _stateDirty = false;
    }
    if (_pendingClockMillis != 0) {
        uint32_t pendingLocal = _pendingLocalMillis;
        if (pendingLocal == 0) {
            pendingLocal = localMillis;
        }
        uint32_t delta = localMillis - pendingLocal;
        clockMillis = _pendingClockMillis + delta;
        anchorClock = clockMillis;
        anchorLocal = localMillis;
        _pendingClockMillis = 0;
        _pendingLocalMillis = 0;
    }
#endif

    if (clockMillis == 0) {
        if (anchorClock != 0 && anchorLocal != 0) {
            uint32_t delta = localMillis - anchorLocal;
            clockMillis = anchorClock + delta;
            anchorClock = clockMillis;
            anchorLocal = localMillis;
        } else {
            clockMillis = localMillis;
            anchorClock = clockMillis;
            anchorLocal = localMillis;
        }
    }

    _lastUpdateClock = anchorClock;
    _lastLocalMillis = anchorLocal;

    if (_frameIntervalMs == 0) {
        _frameIntervalMs = 16;
    }

    // Absolute phase calculation from meshMillis for perfect sync across receivers
    // All devices with the same clockMillis will compute identical animation phase
    _animationPhase = clockMillis * static_cast<uint32_t>(_state.animationSpeed);

    if (_strand) {
        _strand->brightLimit = _state.masterBrightness;
    }

    renderFrame(clockMillis);
    presentFrame();
}

void LedEngine::presentFrame() {
    if (!_strand || !_renderBuffer || !_hwBuffer) {
        return;
    }

#if defined(ARDUINO_ARCH_ESP32)
    if (_bufferMutex) {
        if (xSemaphoreTake(_bufferMutex, portMAX_DELAY) != pdTRUE) {
            return;
        }
    }
#endif

    memcpy(_hwBuffer, _renderBuffer, sizeof(CRGBW) * _config.ledCount);
    LibStrip::updatePixels(_strand);

#if defined(ARDUINO_ARCH_ESP32)
    if (_bufferMutex) {
        xSemaphoreGive(_bufferMutex);
    }
#endif

    calculateFPS();
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
    if (!_initialized || !_strand) {
        return;
    }
    presentFrame();
}

const CRGB* LedEngine::getPreviewPixels() const {
    if (!_initialized || !_previewBuffer || !_hwBuffer) {
        return nullptr;
    }

#if defined(ARDUINO_ARCH_ESP32)
    // Bounded wait: the render task can hold _bufferMutex for the full
    // RMT transmission window (~4 ms for 120 SK6812). If the display
    // poller blocked forever here it would starve button + MIDI updates
    // for that long. Caller (display) just shows the last preview frame
    // on contention.
    if (_bufferMutex) {
        if (xSemaphoreTake(_bufferMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
            return nullptr;
        }
    }
#endif

    for (uint16_t i = 0; i < _config.ledCount; ++i) {
        _previewBuffer[i].r = _hwBuffer[i].r;
        _previewBuffer[i].g = _hwBuffer[i].g;
        _previewBuffer[i].b = _hwBuffer[i].b;
    }

#if defined(ARDUINO_ARCH_ESP32)
    if (_bufferMutex) {
        xSemaphoreGive(_bufferMutex);
    }
#endif

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
    uint32_t travel = (_animationPhase >> 8);
    uint16_t pos = (travel % _config.ledCount);

    switch (_state.direction) {
        case DIR_BACKWARD:
            pos = _config.ledCount - 1 - pos;
            break;
        case DIR_PINGPONG:
            pos = pingPongIndex(travel, _config.ledCount);
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
    uint16_t span = segmentSize * 2;
    if (span == 0) {
        span = 1;
    }
    uint32_t travel = (_animationPhase >> 8);
    uint16_t offset = travel % span;

    switch (_state.direction) {
        case DIR_BACKWARD:
            offset = (span == 0) ? 0 : (span - offset);
            break;
        case DIR_PINGPONG:
            offset = pingPongIndex(travel, span);
            break;
        case DIR_RANDOM:
            offset = random16(span);
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

    // Use animationCtrl to set minimum brightness (depth)
    // animationCtrl=0: oscillates 0-255 (full depth)
    // animationCtrl=255: oscillates 255-255 (no depth, always max)
    uint8_t minBrightness = _state.animationCtrl;
    waveValue = minBrightness + scale8(waveValue, 255 - minBrightness);

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
    
    // Use colorA_hue and colorB_hue to define the hue range
    uint8_t hueStart = _state.colorA_hue;
    uint8_t hueEnd = _state.colorB_hue;
    
    // Calculate hue range (handles wrap-around)
    int16_t hueRange = (int16_t)hueEnd - (int16_t)hueStart;
    if (hueRange < 0) hueRange += 256;  // Wrap around the color wheel

    for (uint16_t i = 0; i < _config.ledCount; ++i) {
        // Map position to hue within the range
        uint8_t hueOffset = (uint8_t)((uint32_t)i * hueRange / _config.ledCount);
        uint8_t hue = hueStart + hueOffset + offset;

        switch (_state.direction) {
            case DIR_BACKWARD:
                hue = hueStart + (hueRange - hueOffset) + offset;
                break;
            case DIR_PINGPONG:
                if (((_animationPhase >> 16) & 0x1) == 1 && _config.ledCount > 0) {
                    uint16_t mirrored = (_config.ledCount - 1) - i;
                    uint8_t mirroredOffset = (uint8_t)((uint32_t)mirrored * hueRange / _config.ledCount);
                    hue = hueStart + mirroredOffset + offset;
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
    if (!_renderBuffer) {
        return;
    }

    // Store raw color values without brightness scaling.
    // Brightness is applied atomically at hardware level via strand->brightLimit
    // in LibStrip::updatePixels() to prevent glitches during MIDI adjustments.
    _renderBuffer[index].r = color.r;
    _renderBuffer[index].g = color.g;
    _renderBuffer[index].b = color.b;
    _renderBuffer[index].w = color.w;
}

void LedEngine::clearLEDs() {
    if (!_renderBuffer) {
        return;
    }
    for (uint16_t i = 0; i < _config.ledCount; ++i) {
        _renderBuffer[i] = {0, 0, 0, 0};
    }
}

void LedEngine::fadePixel(uint16_t index, uint8_t amount) {
    if (index >= _config.ledCount) {
        return;
    }
    if (!_renderBuffer) {
        return;
    }
    _renderBuffer[index].r = scale8(_renderBuffer[index].r, 255 - amount);
    _renderBuffer[index].g = scale8(_renderBuffer[index].g, 255 - amount);
    _renderBuffer[index].b = scale8(_renderBuffer[index].b, 255 - amount);
    _renderBuffer[index].w = scale8(_renderBuffer[index].w, 255 - amount);
}

void LedEngine::applyMirror() {
    if (!_renderBuffer) {
        return;
    }
    switch (_state.mirror) {
        case MIRROR_NONE:
            return;
        case MIRROR_FULL: {
            uint16_t half = _config.ledCount / 2;
            for (uint16_t i = 0; i < half; ++i) {
                _renderBuffer[_config.ledCount - 1 - i] = _renderBuffer[i];
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
                        CRGBW temp = _renderBuffer[start + i];
                        _renderBuffer[start + i] = _renderBuffer[start + quarter - 1 - i];
                        _renderBuffer[start + quarter - 1 - i] = temp;
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
                        CRGBW temp = _renderBuffer[start + i];
                        _renderBuffer[start + i] = _renderBuffer[start + sixth - 1 - i];
                        _renderBuffer[start + sixth - 1 - i] = temp;
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
                        CRGBW temp = _renderBuffer[start + i];
                        _renderBuffer[start + i] = _renderBuffer[start + eighth - 1 - i];
                        _renderBuffer[start + eighth - 1 - i] = temp;
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
    if (!_renderBuffer) {
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
        _renderBuffer[i].r = scale8(_renderBuffer[i].r, dimFactor);
        _renderBuffer[i].g = scale8(_renderBuffer[i].g, dimFactor);
        _renderBuffer[i].b = scale8(_renderBuffer[i].b, dimFactor);
        _renderBuffer[i].w = scale8(_renderBuffer[i].w, dimFactor);
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
