# LedEngine

Shared LED animation engine library for synchronized LED strip control across multiple ESP32 devices.

## Overview

LedEngine is a reusable, platform-agnostic library for driving RGB and RGBW LED strips with complex animations. It supports external clock sources for multi-device synchronization, making it perfect for distributed LED installations.

## Features

- **RGB & RGBW Support**: Works with WS2812 (RGB) and SK6812 (RGBW) strips
- **8 Animation Modes**: Solid, Dual, Chase, Dash, Waveform, Pulse, Rainbow, Sparkle
- **Effects**: Mirror modes, direction control, strobe overlay
- **Clock Sync**: Optional external clock source for multi-device synchronization
- **Dynamic Configuration**: Runtime-configurable LED count, pin, brightness, FPS
- **Zero Dependencies**: Only requires FastLED

## Animation Modes

| Mode | Description |
|------|-------------|
| ANIM_SOLID | Single solid color |
| ANIM_DUAL_SOLID | Two colors (split or gradient blend) |
| ANIM_CHASE | Running chase effect |
| ANIM_DASH | Alternating dash segments |
| ANIM_WAVEFORM | Waveform modulation (sine/triangle/square/sawtooth) |
| ANIM_PULSE | Breathing/pulsing effect |
| ANIM_RAINBOW | Rainbow cycle |
| ANIM_SPARKLE | Random sparkles |

## Usage

### Basic Example

```cpp
#include <LedEngine.h>

// Configure LED engine
LedEngineConfig config;
config.ledCount = 300;
config.dataPin = 2;
config.targetFPS = 60;
config.brightness = 128;
config.enableRGBW = true;  // true for SK6812, false for WS2812

// Create engine
LedEngine* engine = new LedEngine(config);
engine->begin();

void loop() {
    engine->update();
    engine->show();
}
```

### With Clock Synchronization

```cpp
#include <LedEngine.h>
#include <ESPNowMeshClock.h>

ESPNowMeshClock meshClock;
LedEngine* engine;

unsigned long getClockTime() {
    return meshClock.meshMillis();
}

void setup() {
    meshClock.begin(false); // slave mode
    
    LedEngineConfig config;
    // ... configure ...
    
    engine = new LedEngine(config);
    engine->begin();
    engine->setClockSource(getClockTime); // Use synced clock
}

void loop() {
    meshClock.update();
    engine->update();
    engine->show();
}
```

### Control Animation

```cpp
// Set animation parameters
engine->setAnimationMode(ANIM_RAINBOW);
engine->setAnimationSpeed(128);
engine->setMasterBrightness(200);

// Set colors (HSV)
ColorRGBW colorA, colorB;
colorA.fromHSV(0, 255, 255, 0);     // Red
colorB.fromHSV(160, 255, 255, 0);   // Cyan
engine->setColorA(colorA);
engine->setColorB(colorB);

// Apply effects
engine->setMirrorMode(MIRROR_FULL);
engine->setDirectionMode(DIR_BACKWARD);
engine->setStrobeRate(50);
```

## API Reference

### Configuration

```cpp
struct LedEngineConfig {
    uint16_t ledCount;      // Number of LEDs
    uint8_t dataPin;        // GPIO data pin
    uint8_t targetFPS;      // Target frame rate
    uint8_t brightness;     // Default brightness (0-255)
    bool enableRGBW;        // true=SK6812, false=WS2812
};
```

### Core Methods

- `void begin()` - Initialize LED strip
- `void update()` - Calculate next frame (call in loop)
- `void show()` - Display current frame (call after update)

### State Setters

- `void setMasterBrightness(uint8_t brightness)` - 0-255
- `void setAnimationMode(AnimationMode mode)` - See modes above
- `void setAnimationSpeed(uint8_t speed)` - 0-255
- `void setAnimationCtrl(uint8_t ctrl)` - Mode-specific parameter
- `void setStrobeRate(uint8_t rate)` - 0=off, 1-255=faster
- `void setBlendMode(uint8_t blend)` - 0=hard, 255=smooth
- `void setMirrorMode(MirrorMode mirror)` - NONE/FULL/SPLIT2/SPLIT3/SPLIT4
- `void setDirectionMode(DirectionMode dir)` - FORWARD/BACKWARD/PINGPONG/RANDOM
- `void setColorA(const ColorRGBW& color)` - Primary color
- `void setColorB(const ColorRGBW& color)` - Secondary color

### Synchronization

- `void setClockSource(unsigned long (*func)())` - Set external clock function

### Getters

- `uint8_t getMasterBrightness()`
- `AnimationMode getCurrentMode()`
- `uint8_t getAnimationSpeed()`
- `const ColorRGBW& getColorA()`
- `const ColorRGBW& getColorB()`
- `uint32_t getFPS()` - Actual measured FPS
- `uint16_t getLedCount()`

## Color Structure

```cpp
struct ColorRGBW {
    uint8_t r, g, b, w;
    
    void fromHSV(uint8_t hue, uint8_t sat, uint8_t val, uint8_t white = 0);
    CRGB toCRGB() const;
};
```

## Integration

### In Midi2DMXnow
LedEngine provides visual monitoring of the current animation state on an attached LED strip.

### In DMXnow2Strip
LedEngine renders the animation based on received DMX frames, using synchronized clock for perfect multi-device sync.

## Performance

- Supports up to 1000+ LEDs (limited by ESP32 memory and power)
- Configurable FPS (default 60)
- Minimal CPU overhead
- Hardware SPI/RMT for LED communication

## License

Part of the LeslieLEDs project by Hemisphere-Project.
