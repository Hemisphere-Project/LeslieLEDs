# Midi2DMXnow

MIDI-controlled DMX broadcaster with LED strip monitoring.

## Overview

This application receives MIDI control messages (CC and Notes) and converts them to DMX frames that are broadcast wirelessly via ESP-NOW. It also acts as the master clock for synchronizing multiple LED strip receivers.

**Latest**: Includes a 120-pixel RGBW preview strip plus an automatic RGBW boot sweep so wiring issues surface before DMX traffic starts.

## Features

- **MIDI Input**: 
  - USB MIDI (AtomS3)
  - Serial MIDI at 115200 baud (M5Core)
- **DMX Output**: 32-channel DMX frame broadcasted via ESP-NOW
- **LED Monitor**: Visual feedback of current state on the attached 120-pixel strip (RGBW boot sweep at startup)
- **On-Device Display**: Multi-page preview/parameter/log interface with BtnA page toggle and scene notifications
- **Clock Master**: Synchronizes time across all receivers using ESPNowMeshClock
- **Scene Management**: 10 preset slots via MIDI notes 36-45
- **Shared Engine**: Uses LedEngine library for consistent animation across all devices

## Hardware Support

- **M5Stack AtomS3**: USB MIDI native support
- **M5Stack Core**: Serial MIDI via GPIO pins

Both builds assume a 120-pixel SK6812 RGBW strip: GPIO2 on AtomS3, GPIO26 on M5Core. Override via `build_flags` if you need a different length or pin, but keep the sender and receivers aligned for identical previews.

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
pio run -e m5stack_atoms3  # AtomS3 / USB MIDI / GPIO2 preview strip
pio run -e m5core          # M5Core / Serial MIDI / GPIO26 preview strip
```

## Usage

1. Flash firmware to your device
2. Connect LED strip to monitoring pin (GPIO2 for AtomS3, GPIO26 for M5Core)
3. Connect MIDI controller (USB or Serial)
4. Power on receivers (DMXnow2Strip)
5. Control LEDs via MIDI!

MIDI messages are converted to DMX frames and broadcast at ~30 Hz. The attached LED strip displays the same animation that will appear on all receivers, providing instant visual feedback. A quick red→green→blue→white sweep runs right after boot so you can confirm the preview strip wiring before MIDI control begins.

### Display Pages

- **Preview**: Live LED snake preview plus FPS and LED count
- **Parameters**: Active mode, brightness, FPS, colors, and animation speed
- **MIDI Log**: Rolling list of recent CC/Note events for quick troubleshooting

Use `BtnA` on the controller to cycle through pages. Scene loads/saves trigger a full-screen notification so you always know which preset changed.
