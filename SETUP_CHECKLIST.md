# Setup and Testing Checklist

## Prerequisites

- [ ] PlatformIO installed
- [ ] ESP32 hardware (AtomS3, M5Core, or generic ESP32)
- [ ] SK6812 RGBW LED strips
- [ ] MIDI controller (USB or Serial)
- [ ] USB cables for programming and power

## Library Dependencies

Both projects require these libraries (auto-installed by PlatformIO):

- [ ] ESPNowDMX: https://github.com/Hemisphere-Project/ESPNowDMX
- [ ] ESPNowMeshClock: https://github.com/Hemisphere-Project/ESPNowMeshClock
- [ ] FastLED (receiver only)
- [ ] M5Unified (M5 platforms only)

### Contributing fixes upstream

1. Fork `https://github.com/Hemisphere-Project/ESPNowDMX` and clone your fork next to this repo (e.g., `../shared_libs/ESPNowDMX`).
2. Create a feature branch, make changes, and keep that folder checked into *its* git repo only (do **not** re-add it to this project).
3. From `Midi2DMXnow` or `DMXnow2Strip`, delete `.pio/libdeps/<env>/ESPNowDMX` to force PlatformIO to pick up the local fork via the existing `lib_extra_dirs = ../shared_libs` entry.
4. Run the usual builds (`pio run -e m5stack_atoms3`, `pio run -e m5core`, `pio run -e atom_lite`, etc.) until they succeed.
5. Push the branch to your fork, open a PR against Hemisphere-Project/ESPNowDMX, and once merged, remove the temporary local folder so future builds use the upstream commit again.

## Build and Flash

### Midi2DMXnow (Sender)

```bash
cd Midi2DMXnow

# For AtomS3 (USB MIDI)
pio run -e m5stack_atoms3 -t upload

# For M5Core (Serial MIDI)
pio run -e m5core -t upload
```

- [ ] Build successful
- [ ] Flash successful
- [ ] Device boots and shows "Midi2DMXnow" on display

### DMXnow2Strip (Receivers)

```bash
cd DMXnow2Strip

# For AtomS3
pio run -e m5stack_atoms3 -t upload

# For M5Core
pio run -e m5core -t upload

# For custom ESP32
pio run -e esp32_custom -t upload
```

Repeat for each receiver device:
- [ ] Receiver #1: Build/flash successful
- [ ] Receiver #2: Build/flash successful
- [ ] Receiver #N: Build/flash successful

## Hardware Connections

### Sender (Midi2DMXnow)
- [ ] USB power connected
- [ ] MIDI controller connected (USB or Serial)
- [ ] Device displays "Ready for MIDI"

### Receivers (DMXnow2Strip)
- [ ] LED strip data pin connected:
  - AtomS3: GPIO2
  - M5Core: GPIO26
  - Custom: As configured
- [ ] LED strip power supply connected (separate 5V)
- [ ] LED strip ground connected to ESP32 ground
- [ ] Device displays "Waiting for DMX"

## Testing Procedure

### 1. Power On Sequence
- [ ] Power on all receivers first
- [ ] Power on sender last
- [ ] All receivers show "Clock: Slave" status

### 2. MIDI Connection Test
- [ ] Send MIDI CC1 (Master Brightness): LEDs respond
- [ ] Send MIDI CC8 (Animation Mode): Animation changes
- [ ] Send MIDI CC2 (Speed): Animation speed changes
- [ ] Send MIDI CC20 (Color A Hue): Color changes

### 3. Synchronization Test
- [ ] All receivers show same color
- [ ] All receivers show same animation
- [ ] Animation moves in perfect sync across all strips
- [ ] No visible timing differences

### 4. Scene Preset Test
- [ ] Hold CC127 > 0 (Scene Save Mode)
- [ ] Press Note 36: Saves current state as Scene 1
- [ ] Change settings
- [ ] Press Note 36 again: Recalls Scene 1
- [ ] Verify all receivers change together

### 5. Wireless Range Test
- [ ] Move receivers apart
- [ ] Verify DMX reception at 5m
- [ ] Verify DMX reception at 10m
- [ ] Verify DMX reception at 20m
- [ ] Note maximum working distance: _____m

## Troubleshooting

### No MIDI Response
- [ ] Check MIDI device is recognized (USB MIDI)
- [ ] Check baud rate 115200 (Serial MIDI)
- [ ] Check Serial TX/RX connections (M5Core)
- [ ] Monitor serial output for MIDI messages

### Receivers Not Responding
- [ ] Check ESP-NOW initialized (serial debug)
- [ ] Verify sender is broadcasting (LED blinks)
- [ ] Check receivers show "DMX: Waiting" status
- [ ] Verify WiFi channel compatibility

### Animations Not Synced
- [ ] Verify MeshClock master/slave status
- [ ] Check clock sync in serial output
- [ ] Restart all devices (receivers first, then sender)
- [ ] Verify all on same WiFi channel

### LEDs Not Lighting
- [ ] Check LED strip power supply (5V, adequate amps)
- [ ] Verify data pin connection (GPIO correct)
- [ ] Check LED strip ground to ESP32
- [ ] Test with simple animation (solid color)
- [ ] Verify LED_COUNT matches actual strip length

## Expected Serial Output

### Sender (Midi2DMXnow)
```
=== Midi2DMXnow Starting ===
Platform: AtomS3
MIDI Mode: USB MIDI
Setup complete - Ready for MIDI
Broadcasting DMX over ESP-NOW
MeshClock master mode enabled
MIDI CC - Ch:1 CC:1 Val:64
Sent 100 DMX frames, Clock: 3456 ms
```

### Receiver (DMXnow2Strip)
```
=== DMXnow2Strip Starting ===
Platform: AtomS3
LED Count: 300
LED_DATA_PIN: GPIO 2
Setup complete
Waiting for DMX over ESP-NOW
MeshClock slave mode enabled
DMX: Connected, Clock: 3456 ms, FPS: 60
```

## Success Criteria

✓ Sender receives MIDI and displays messages  
✓ Sender broadcasts DMX at ~30 Hz  
✓ All receivers connect and sync clock  
✓ All receivers display identical animations  
✓ Animations stay synchronized over time  
✓ Scene presets work across all receivers  
✓ Range meets project requirements  

## Notes

- ESP-NOW has a limit of ~20 paired devices (not applicable in broadcast mode)
- Broadcast mode has no pairing required, unlimited receivers
- Clock sync accuracy improves after ~10 seconds of runtime
- LED strips require adequate power (300 LEDs ≈ 18A @ full white)
- Use separate power supply for LED strips, not ESP32 power

## Performance Targets

- DMX frame rate: 30 Hz (33ms interval)
- LED refresh rate: 60 FPS
- Clock sync accuracy: <5ms
- Radio latency: <20ms typical
- Maximum receivers: Unlimited (broadcast mode)
