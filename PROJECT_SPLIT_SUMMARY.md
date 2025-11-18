# Project Split Summary

## What Was Done

The original `Midi2Strip` monolithic project has been split into two separate wireless applications with synchronized operation.

## New Projects

### 1. **Midi2DMXnow** - MIDI to DMX Transmitter
- Location: `/Midi2DMXnow/`
- Receives MIDI (USB or Serial)
- Converts to 32-channel DMX frame
- Broadcasts via ESP-NOW at ~30Hz
- Acts as master clock
- Platforms: AtomS3 (USB MIDI), M5Core (Serial MIDI)

### 2. **DMXnow2Strip** - DMX to LED Strip Receiver
- Location: `/DMXnow2Strip/`  
- Receives DMX via ESP-NOW
- Drives SK6812 RGBW LED strips
- Synchronized clock (slave mode)
- Platforms: AtomS3, M5Core, Generic ESP32

## Key Implementation Details

### DMX Protocol
- 32 channels used (0-15 for core params, 16-31 reserved)
- Maps directly to original MIDI CC structure
- Channels 0-7: Global animation parameters
- Channels 8-11: Color A (HSVW)
- Channels 12-15: Color B (HSVW)

### Clock Synchronization
Both apps use `ESPNowMeshClock`:
- **Sender**: Master mode - broadcasts time
- **Receivers**: Slave mode - sync to master

Animation phase calculated as:
```cpp
unsigned long syncTime = meshClock.meshMillis();
_animationPhase = (syncTime * _animationSpeed) / 10;
```

This ensures identical animation frames across all receivers given the same DMX state.

### Libraries Required
- **ESPNowDMX**: https://github.com/Hemisphere-Project/ESPNowDMX
- **ESPNowMeshClock**: https://github.com/Hemisphere-Project/ESPNowMeshClock
- FastLED (receiver only)
- M5Unified (M5 platforms)

### Code Reuse
From original project:
- MIDI handlers (adapted for DMX state)
- LED animation engine (adapted for synced clock)
- Animation modes and effects
- Scene preset system

### Key Changes
1. **Sender**: LEDController → DMXState (manages state, no rendering)
2. **Receiver**: Added DMXReceiver to map channels back to LED params
3. **Timing**: Local `millis()` → `meshClock.meshMillis()` for animations
4. **Communication**: Direct MIDI control → MIDI→DMX→LEDs pipeline

## File Structure

```
Midi2DMXnow/
├── platformio.ini (atoms3, m5core)
├── include/config.h (DMX channel definitions)
├── src/
│   ├── main.cpp (MIDI + DMX sender + MeshClock master)
│   ├── dmx_state.h/cpp (state management)
│   ├── midi_handler.h/cpp (USB MIDI)
│   └── serial_midi_handler.h/cpp (Serial MIDI)
└── README.md

DMXnow2Strip/
├── platformio.ini (atoms3, m5core, esp32_custom)
├── include/config.h (LED + DMX config)
├── src/
│   ├── main.cpp (DMX receiver + LEDs + MeshClock slave)
│   ├── dmx_receiver.h/cpp (DMX to LED params)
│   └── led_controller.h/cpp (animation engine)
└── README.md
```

## Original Project
`Midi2Strip/` - Kept as reference, unchanged

## Benefits
✓ Scalable (unlimited receivers)  
✓ Wireless (ESP-NOW radio)  
✓ Synchronized (perfect timing)  
✓ Flexible (mix hardware types)  
✓ Long range (~200m outdoors)

## Testing
Compile both projects and flash to devices. The sender will broadcast DMX and clock data. Receivers will auto-synchronize and display identical animations.
