# Midi2DMXnow

MIDI-controlled DMX broadcaster with LED strip monitoring.

## Overview

This application receives MIDI control messages (CC and Notes) and converts them to DMX frames that are broadcasted wirelessly via ESP-NOW. It also acts as the master clock for synchronizing multiple LED strip receivers.

**New**: Includes an onboard LED strip for real-time visual monitoring of the current animation state.

## Features

- **MIDI Input**: 
  - USB MIDI (AtomS3)
  - Serial MIDI at 115200 baud (M5Core)
- **DMX Output**: 32-channel DMX frame broadcasted via ESP-NOW
- **LED Monitor**: Visual feedback of current state on attached LED strip
- **On-Device Display**: Multi-page preview/parameter/log interface with BtnA page toggle and scene notifications
- **Clock Master**: Synchronizes time across all receivers using ESPNowMeshClock
- **Scene Management**: 10 preset slots via MIDI notes 36-45
- **Shared Engine**: Uses LedEngine library for consistent animation across all devices

## Hardware Support

- **M5Stack AtomS3**: USB MIDI native support
- **M5Stack Core**: Serial MIDI via GPIO pins

## DMX Channel Layout

| Channel | Parameter | Range |
|---------|-----------|-------|
| 0 | Master Brightness | 0-255 |
| 1 | Animation Mode | 0-255 |
| 2 | Animation Speed | 0-255 |
| 3 | Animation Control | 0-255 |
| 4 | Strobe Rate | 0-255 |
| 5 | Blend Mode | 0-255 |
| 6 | Mirror Mode | 0-255 |
| 7 | Direction | 0-255 |
| 8-11 | Color A (H,S,V,W) | 0-255 each |
| 12-15 | Color B (H,S,V,W) | 0-255 each |
| 16-31 | Reserved | - |

## Dependencies

- M5Unified
- FastLED
- LedEngine (shared library in ../LEDengine)
- ESPNowDMX (Hemisphere-Project)
- ESPNowMeshClock (Hemisphere-Project)

## Building

```bash
pio run -e m5stack_atoms3  # For AtomS3
pio run -e m5core          # For M5Core
```

## Usage

1. Flash firmware to your device
2. Connect LED strip to monitoring pin (GPIO2 for AtomS3, GPIO26 for M5Core)
3. Connect MIDI controller (USB or Serial)
4. Power on receivers (DMXnow2Strip)
5. Control LEDs via MIDI!

MIDI messages are converted to DMX frames and broadcasted at ~30Hz. The attached LED strip displays the same animation that will appear on all receivers, providing instant visual feedback.

### Display Pages

- **Preview**: Live LED snake preview plus FPS and LED count
- **Parameters**: Active mode, brightness, FPS, colors, and animation speed
- **MIDI Log**: Rolling list of recent CC/Note events for quick troubleshooting

Use `BtnA` on the controller to cycle through pages. Scene loads/saves trigger a full-screen notification so you always know which preset changed.
