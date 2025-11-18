# Quick Reference: LED Engine Integration

## Project Structure

```
LeslieLEDs/
├── LEDengine/              ← Shared animation library
├── Midi2DMXnow/            ← Sender with LED monitoring
├── DMXnow2Strip/           ← Receiver using shared library
└── Midi2Strip/             ← Original (reference only)
```

## Building

Both projects automatically use the shared library from `LEDengine/`:

```bash
cd Midi2DMXnow
pio run -e m5stack_atoms3    # Sender with monitoring

cd DMXnow2Strip  
pio run -e m5stack_atoms3    # Receiver
```

## LED Connections

### Sender (Midi2DMXnow)
- **AtomS3**: GPIO2 → LED Data
- **M5Core**: GPIO26 → LED Data
- Shows live preview of animation

### Receivers (DMXnow2Strip)
- **AtomS3**: GPIO2 → LED Data
- **M5Core**: GPIO26 → LED Data
- **Custom**: Configurable via platformio.ini

## Using LedEngine in New Projects

### 1. Create Project

```bash
mkdir MyLEDProject
cd MyLEDProject
```

### 2. platformio.ini

```ini
[env:myboard]
platform = espressif32
board = esp32dev
framework = arduino

lib_deps = 
    fastled/FastLED@^3.10.3
```

### 3. Include Library

```cpp
#include <LedEngine.h>

// Configure
LedEngineConfig config;
config.ledCount = 150;
config.dataPin = 5;
config.targetFPS = 60;
config.brightness = 128;
config.enableRGBW = true;  // SK6812=true, WS2812=false

// Create instance
LedEngine* engine = new LedEngine(config);

void setup() {
    engine->begin();
    
    // Set initial state
    engine->setAnimationMode(ANIM_RAINBOW);
    engine->setAnimationSpeed(64);
}

void loop() {
    engine->update();
    engine->show();
}
```

### 4. With Clock Sync

```cpp
#include <ESPNowMeshClock.h>

ESPNowMeshClock meshClock;

unsigned long getClockTime() {
    return meshClock.meshMillis();
}

void setup() {
    meshClock.begin(false);  // slave
    engine->setClockSource(getClockTime);
}

void loop() {
    meshClock.update();
    engine->update();
    engine->show();
}
```

## Animation Control

```cpp
// Mode
engine->setAnimationMode(ANIM_SOLID);       // Solid color
engine->setAnimationMode(ANIM_CHASE);       // Chase effect
engine->setAnimationMode(ANIM_RAINBOW);     // Rainbow
// ... see LedEngine.h for all modes

// Colors (HSV)
ColorRGBW red, blue;
red.fromHSV(0, 255, 255, 0);      // H=0, S=255, V=255, W=0
blue.fromHSV(160, 255, 255, 0);
engine->setColorA(red);
engine->setColorB(blue);

// Parameters
engine->setMasterBrightness(200);  // 0-255
engine->setAnimationSpeed(128);    // 0-255
engine->setAnimationCtrl(64);      // Mode-specific
engine->setStrobeRate(50);         // 0=off, 1-255

// Effects
engine->setMirrorMode(MIRROR_FULL);      // Mirror effects
engine->setDirectionMode(DIR_BACKWARD);  // Reverse
engine->setBlendMode(128);               // Gradient blend
```

## Enums

```cpp
// Animation Modes
ANIM_SOLID, ANIM_DUAL_SOLID, ANIM_CHASE, ANIM_DASH,
ANIM_WAVEFORM, ANIM_PULSE, ANIM_RAINBOW, ANIM_SPARKLE

// Mirror Modes
MIRROR_NONE, MIRROR_FULL, MIRROR_SPLIT2, MIRROR_SPLIT3, MIRROR_SPLIT4

// Direction Modes
DIR_FORWARD, DIR_BACKWARD, DIR_PINGPONG, DIR_RANDOM

// Waveform Types (for ANIM_WAVEFORM)
WAVE_SINE, WAVE_TRIANGLE, WAVE_SQUARE, WAVE_SAWTOOTH
```

## DMX to LedEngine Adapter

If receiving DMX frames:

```cpp
#include "dmx_to_ledengine.h"

DMXToLedEngine* adapter = new DMXToLedEngine(engine);

// When DMX frame received:
void onDMXFrame(const uint8_t* data, uint16_t size) {
    adapter->applyDMXFrame(data, size);
}
```

## Troubleshooting

### LEDs not lighting
- Check data pin matches config
- Verify LED strip power (separate 5V supply)
- Check ground connection between ESP32 and strip
- Test with simple solid color first

### Animations not syncing
- Verify clock sync: `Serial.println(meshClock.meshMillis())`
- Check setClockSource() was called
- Ensure all devices on same WiFi channel
- Verify master/slave modes correct

### Compilation errors
- Ensure FastLED is installed
- Check LEDengine/ exists in project root
- Verify #include <LedEngine.h> (not "LedEngine.h")
- Clean build: `pio run -t clean`

## Performance Tips

- Lower FPS for many LEDs: `config.targetFPS = 30;`
- Reduce LED count if memory issues
- Disable effects if CPU constrained
- Use lower brightness to reduce power draw

## Full API

See `LEDengine/README.md` for complete API reference.
