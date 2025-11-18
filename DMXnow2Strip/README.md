# DMXnow2Strip

Wireless DMX receiver that drives synchronized LED strip animations.

## Overview

This application receives DMX frames via ESP-NOW and translates them into LED strip animations. Uses a synchronized clock (ESPNowMeshClock) to ensure all receivers display identical animations perfectly in sync, even when distributed across multiple devices.

## Features

- **DMX Input**: Receives 32-channel DMX via ESP-NOW wireless
- **LED Output**: Drives SK6812 RGBW LED strips (default 300 LEDs)
- **Clock Slave**: Synchronizes animation timing with master clock
- **Shared Engine**: Uses LedEngine library for consistent animations
- **Animation Engine**: 8 animation modes with extensive control
- **Atom Lite Focus**: Tuned for the M5Stack Atom Lite receiver hardware

## Hardware Support

- **M5Stack Atom Lite**: Uses GPIO26 for LED data (override via `LED_DATA_PIN` if needed)

## Animation Modes

0. Solid color
1. Dual solid (split/blend)
2. Chase/running lights
3. Dash/segments
4. Waveform (sine/triangle/square/sawtooth)
5. Pulse/breathing
6. Rainbow cycle
7. Sparkle

## Configuration

- Default LED data pin is **GPIO26** with **300 SK6812 RGBW** LEDs.
- Override `LED_DATA_PIN` or `LED_COUNT` by adding extra `build_flags` in `platformio.ini` or via the PlatformIO CLI:  
    `pio run -e atom_lite --project-option "build_flags=-DLED_DATA_PIN=23 -DLED_COUNT=120"`

## Dependencies

- FastLED
- M5Unified (for M5 platforms)
- LedEngine (shared library in ../LEDengine)
- ESPNowDMX (Hemisphere-Project)
- ESPNowMeshClock (Hemisphere-Project)

## Building

```bash
pio run -e atom_lite
```

## How It Works

1. **Receives DMX**: Listens for ESP-NOW DMX broadcasts
2. **Maps to Engine**: Converts DMX channels to LedEngine parameters via adapter
3. **Synchronized Time**: Uses MeshClock for consistent animation phase
4. **Renders Frame**: LedEngine generates LED colors based on synced clock
5. **Identical Output**: All receivers show the same animation simultaneously

The key to synchronization is the shared **LedEngine** library using `meshClock.meshMillis()` instead of `millis()` for animation calculations. This ensures that given the same DMX state, all receivers produce identical output at the exact same moment.

## Architecture

```
[Midi2DMXnow] --ESP-NOW--> [DMXnow2Strip #1]
      |                    [DMXnow2Strip #2]
   MeshClock               [DMXnow2Strip #N]
    (Master)                  (Slaves)
```

All receivers synchronize their clocks with the master and use that clock time to drive animations, ensuring perfect synchronization across the entire system.
