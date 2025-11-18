# LeslieLEDs Split Architecture

The original Midi2Strip project has been split into two separate applications for wireless distributed LED control.

## Architecture Overview

```
MIDI Controller
      |
      v
┌─────────────────┐
│  Midi2DMXnow    │  (Master)
│  - MIDI Input   │
│  - DMX Sender   │
│  - Clock Master │
└────────┬────────┘
         │ ESP-NOW Broadcast
         │ (DMX + Clock)
         v
    ┌────┴────┬────────┬────────┐
    v         v        v        v
┌────────┐ ┌────────┐ ┌────────┐
│ DMXnow │ │ DMXnow │ │ DMXnow │  (Slaves)
│ 2Strip │ │ 2Strip │ │ 2Strip │
│   #1   │ │   #2   │ │   #N   │
└────────┘ └────────┘ └────────┘
LED Strip   LED Strip   LED Strip
```

## Applications

### 1. Midi2DMXnow (Transmitter)

**Purpose**: Receive MIDI, broadcast DMX state wirelessly, monitor output

**Key Features**:
- MIDI input (USB or Serial)
- Converts MIDI CC/Notes to 32-channel DMX frame
- Broadcasts via ESP-NOW at ~30Hz
- Master clock for synchronization
- Scene presets (10 slots)
- **LED monitoring strip** for visual feedback (uses shared LedEngine)

**Hardware**: M5Stack AtomS3, M5Stack Core

**Output**: DMX over ESP-NOW radio + Local LED strip monitoring

---

### 2. DMXnow2Strip (Receiver)

**Purpose**: Receive DMX, render synchronized LED animations

**Key Features**:
- DMX input via ESP-NOW
- Maps DMX to animation parameters
- Synchronized clock (slave mode)
- 8 animation modes with effects
- Multi-platform support
- Uses shared **LedEngine** library

**Hardware**: M5Stack AtomS3, M5Stack Core, Generic ESP32

**Output**: SK6812 RGBW LED strips

## Shared LED Engine Library

Both applications now use a shared **LedEngine** library (`LEDengine/`) for consistent animation rendering:

- **Reusable**: Single implementation used by both sender and receivers
- **Synchronized**: Accepts external clock source for perfect timing
- **Flexible**: Configurable LED count, pin, brightness, FPS
- **Complete**: All 8 animation modes with effects built-in

**Benefits**:
- Bug fixes and improvements automatically apply to all projects
- Guaranteed identical animations across sender monitoring and receivers
- Reduced code duplication
- Easier maintenance and testing

## Synchronization

The system uses **ESPNowMeshClock** and the shared **LedEngine** library to ensure perfect timing across all devices:

1. **Master** (Midi2DMXnow): Maintains authoritative time
2. **Slaves** (DMXnow2Strip): Sync their clocks to master
3. **Animation Phase**: Calculated from synced time, not local `millis()`
4. **Result**: Identical animations across all receivers

Formula: `animationPhase = (syncTime * speed) / 10`

This means given the same DMX state, all receivers compute the exact same animation frame at the exact same moment.

## Original Project

The original **Midi2Strip** project remains in its folder as reference. It was a single-device solution combining MIDI input and LED control.

## Benefits of Split Architecture

- **Scalability**: Add unlimited receivers
- **Wireless**: No DMX cables needed
- **Synchronization**: Perfect timing across devices
- **Flexibility**: Mix different hardware platforms
- **Range**: ESP-NOW works up to ~200m outdoors

## Quick Start

1. **Build Sender**:
   ```bash
   cd Midi2DMXnow
   pio run -e m5stack_atoms3 -t upload
   ```

2. **Build Receivers** (repeat for each device):
   ```bash
   cd DMXnow2Strip
   pio run -e m5stack_atoms3 -t upload
   ```

3. **Connect MIDI** to sender device

4. **Power on** all receivers

5. **Control** via MIDI!

## MIDI Mapping

See `Midi2Strip/MIDI_MAPPING.md` for complete CC and Note assignments.

Key controls:
- **CC1**: Master brightness
- **CC2**: Animation speed
- **CC8**: Animation mode
- **CC20-23**: Color A (HSVW)
- **CC30-33**: Color B (HSVW)
- **Notes 36-45**: Scene recall/save

## Dependencies

All applications require:
- https://github.com/Hemisphere-Project/ESPNowDMX
- https://github.com/Hemisphere-Project/ESPNowMeshClock
- **LedEngine** (shared library in `LEDengine/`)

Sender also needs:
- M5Unified

Receivers also need:
- FastLED (included via LedEngine)
- M5Unified (for M5 platforms)
