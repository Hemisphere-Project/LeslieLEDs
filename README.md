# LeslieLEDs

MIDI-controlled RGBW LED strip lighting system for live music performances.

## Hardware

- **Controller**: M5Stack AtomS3 (ESP32-S3)
- **LED Strip**: 300x SK6812 RGBW pixels
- **Connection**: GPIO 2 (G2 on AtomS3)
- **Power**: External 5V supply for LED strip
- **Interface**: USB MIDI + 128x128 LCD display

## Features

- 🎵 Single-channel MIDI control (optimized for Ableton Live)
- 🎨 Dual-color RGBW support (Color A + Color B)
- ⚡ 8 built-in animation modes (+ waveform variations)
- 🎭 10 scene presets (Note triggers)
- 💫 Global strobe overlay (applies to any animation)
- 🪞 5-level mirror modes (none/full/split2/split3/split4)
- ➡️ 4 direction modes (forward/backward/pingpong/random)
- 📊 Real-time FPS monitoring on display
- 🔄 Non-blocking RMT driver for smooth operation

---

## Architecture

### File Structure

```
include/
  └── config.h              # Hardware pins, MIDI mappings, constants
src/
  ├── main.cpp              # Main setup/loop coordination
  ├── midi_handler.h/cpp    # USB MIDI input processing
  ├── led_controller.h/cpp  # LED strip control + animations
  └── display_handler.h/cpp # LCD screen updates
```

### Component Diagram

```
┌─────────────────┐
│  Ableton Live   │
│   MIDI Track    │
│   (Channel 1)   │
└────────┬────────┘
         │ USB MIDI
         ▼
┌─────────────────────────────────────────┐
│           AtomS3 (ESP32-S3)             │
│                                         │
│  ┌──────────────┐    ┌──────────────┐  │
│  │ MIDI Handler │───▶│LED Controller│  │
│  └──────┬───────┘    └──────┬───────┘  │
│         │                   │          │
│         │                   │ RMT      │
│         ▼                   ▼          │
│  ┌──────────────┐    ┌──────────────┐  │
│  │   Display    │    │  300 LEDs    │  │
│  │   Handler    │    │   SK6812     │  │
│  └──────────────┘    └──────────────┘  │
└─────────────────────────────────────────┘
```

---

## MIDI → LED Animation Flow

### 1. MIDI Input Processing

**Path**: `midi_handler.cpp` → `handleControlChange()`

```
USB MIDI Packet
     ↓
Extract: channel, CC#, value
     ↓
Route by CC# range:
  • CC 0-19   → Global controls
  • CC 20-29  → Color A
  • CC 30-39  → Color B
     ↓
Call: ledController->handleGlobalCC() or handleColorCC()
```

### 2. Parameter Update

**Path**: `led_controller.cpp` → `handleGlobalCC()` / `handleColorCC()`

```
Receive CC# and value
     ↓
Update internal state:
  • _masterBrightness
  • _animationSpeed
  • _colorA / _colorB (RGBW)
  • _currentMode
  • _mirror, _reverse, etc.
     ↓
Changes take effect on next frame
```

### 3. Animation Rendering

**Path**: `led_controller.cpp` → `update()` → `render[Mode]()`

```
Loop (every ~16ms for 60 FPS):
     ↓
Check frame rate limit
     ↓
Update _animationPhase based on _animationSpeed
     ↓
Call renderer for _currentMode:
  • renderSolid()
  • renderDualSolid()
  • renderChase()
  • renderDash()
  • renderWaveform()
  • renderPulse()
  • renderRainbow()
  • renderSparkle()
     ↓
Apply effects:
  • Mirror mode (5 levels: none/full/split2/split3/split4)
  • Strobe overlay (PWM dimming if CC4 > 0)
     ↓
FastLED.show() → RMT driver → Physical LEDs
```

### 4. Example: Chase Animation with Direction

```cpp
// Simplified from renderChase()
void renderChase() {
    uint16_t segmentSize = map(_animationCtrl, 0, 127, 1, LED_COUNT / 4);
    uint16_t pos = (_animationPhase >> 8) % LED_COUNT;
    
    // Apply direction mode
    switch (_direction) {
        case DIR_BACKWARD:
            pos = LED_COUNT - 1 - pos;
            break;
        case DIR_PINGPONG:
            if (((_animationPhase >> 8) / LED_COUNT) % 2 == 1) {
                pos = 0; // Snap back to start
            }
            break;
        case DIR_RANDOM:
            pos = random16(LED_COUNT);
            break;
    }
    
    // Fade all LEDs
    for (uint16_t i = 0; i < LED_COUNT; i++) {
        _leds[i].fadeToBlackBy(20);
    }
    
    // Draw moving segment in Color A
    for (uint8_t i = 0; i < segmentSize && pos + i < LED_COUNT; i++) {
        setPixelRGBW(pos + i, _colorA);
    }
}
```

### 5. Scene Triggers

**Path**: Note On → `loadScene()` → Apply preset parameters

```
MIDI Note C1-A1 (36-45)
     ↓
Look up scene preset array
     ↓
Load all parameters:
  • Animation mode
  • Colors A & B
  • Speed, blend, mirror, direction, animCtrl
     ↓
Immediate switch (no fade)
```

---

## Key Design Decisions

### Why Single MIDI Channel?
- **Simpler Ableton workflow**: One MIDI track instead of three
- **Easier automation**: All parameters in one clip
- **20-CC blocks**: Room for expansion (60 CCs reserved)

### Why RMT Driver?
- **Non-blocking**: CPU free to process MIDI while LEDs update
- **Hardware timing**: Precise SK6812 signal without bit-banging
- **Built into FastLED**: Zero additional code complexity

### Why Dual Colors?
- **Versatile animations**: Solid, split, blend, dash patterns
- **Live performance**: Quick color changes via MIDI CC
- **RGBW support**: Pure white channel for cleaner whites

### Why Scene Presets?
- **Instant recall**: One MIDI note = full lighting state
- **Ableton clips**: Map to pads/keys for live triggering
- **Pre-show prep**: Build 10 scenes for ~1hr show

---

## Performance

- **Target FPS**: 30-60 (configurable in `config.h`)
- **MIDI Latency**: <10ms typical
- **LED Update**: Non-blocking via RMT
- **Display Update**: 50ms intervals (20 FPS)

### Frame Budget (at 60 FPS = 16.6ms)
- MIDI processing: ~0.5ms
- Animation calc: ~2-5ms (depends on mode)
- FastLED.show(): ~9ms (for 300 LEDs)
- Display: ~3ms (every 3rd frame)
- **Total**: ~14.5ms ✅ (headroom for stability)

---

## Quick Start

### 1. Hardware Setup
```
AtomS3 GPIO2 (G2) → LED Strip Data In
5V Power Supply   → LED Strip VCC/GND
AtomS3 GND        → LED Strip GND (common ground)
```

### 2. Ableton Live Setup
1. Create MIDI track, set output to "LeslieLEDs"
2. Set MIDI channel to 1
3. Map CC1 (brightness) to a fader
4. Map CC20-23 (Color A) to knobs
5. Map notes C1-A1 to clip slots for scene triggers

### 3. Basic Test
```
CC1 = 127   → Full brightness
CC20 = 0    → Red hue
CC21 = 127  → Full saturation
CC22 = 127  → Full value
→ Strip should show solid red at full brightness
```

---

## MIDI Mapping Reference

See [MIDI_MAPPING.md](MIDI_MAPPING.md) for complete CC chart.

**Quick Reference:**
- CC1: Master Brightness (0-255 range)
- CC2: Animation Speed
- CC3: Animation Ctrl (varies by mode)
- CC4: Strobe Rate (global overlay)
- CC5: Blend Mode
- CC6: Mirror Mode (5 levels)
- CC7: Direction (forward/backward/pingpong/random)
- CC8: Animation Mode (0=blackout, 1-9=solid, 10-19=dual, etc.)
- CC20-23: Color A (Hue, Sat, Val, White)
- CC30-33: Color B (Hue, Sat, Val, White)
- Notes C1-A1: Scene 1-10
- Note C2: Blackout

---

## Development

### Build & Upload
```bash
pio run --target upload
```

### Monitor Serial Output
```bash
pio device monitor
```

### Debugging
Enable `DEBUG_MODE` in `config.h` for serial logging.

---

## License

MIT License - See LICENSE file
