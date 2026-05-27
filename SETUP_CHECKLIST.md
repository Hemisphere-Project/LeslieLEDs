# Setup and Testing Checklist

## Prerequisites

- [ ] PlatformIO installed
- [ ] ESP32 hardware (AtomS3, M5Core, or generic ESP32)
- [ ] SK6812 RGBW LED strips
- [ ] MIDI controller (USB or Serial)
- [ ] USB cables for programming and power

## Library Dependencies

Both PlatformIO projects pick up:

- FastLED, M5Unified — fetched from the PlatformIO registry on first build.
- `leslie_protocol/` — header-only, in-repo at `shared/leslie_protocol/`. Holds the DMX channel layout that both firmwares must agree on.
- ESPNowDMX, ESPNowMeshClock — git clones inside `shared_libs/`. **This folder is gitignored**, so a fresh clone of LeslieLEDs will not have them. Clone manually:

```bash
mkdir -p shared_libs
git clone https://github.com/Hemisphere-Project/ESPNowDMX        shared_libs/ESPNowDMX
git clone https://github.com/Hemisphere-Project/ESPNowMeshClock  shared_libs/ESPNowMeshClock
```

Both PIO envs use `lib_extra_dirs = ../LEDengine ../shared ../shared_libs`, so once the two clones exist the build resolves them automatically.

### Contributing fixes upstream

1. Make changes inside `shared_libs/ESPNowDMX` (or `…/ESPNowMeshClock`) — it is its own git checkout.
2. Rebuild `Midi2DMXnow` and `DMXnow2Strip` until they succeed.
3. Push the branch to your fork and open a PR against `Hemisphere-Project/ESPNowDMX` / `…MeshClock`.
4. Once merged, `git pull` inside `shared_libs/<lib>` to switch back to upstream `main`.

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
- [ ] Confirm each device plays the fast RGBW boot sweep before entering "Waiting" state
- [ ] On Atom Lite slaves the onboard LED goes RED during setup, then GREEN once init completes
- [ ] All receivers report a synced MeshClock state once the sender starts broadcasting

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
- [ ] Hold CC127 ≥ 64 (Scene Save Mode)
- [ ] Press Note 36: Saves current state as Scene 1 (of 20 — notes 36–55)
- [ ] Change settings
- [ ] Press Note 36 again with CC127 = 0: Recalls Scene 1
- [ ] Verify all receivers change together
- [ ] Release the scene note — slaves go to blackout while held off; press again to re-trigger

### 5. Wireless Range Test
- [ ] Move receivers apart
- [ ] Verify DMX reception at 5m
- [ ] Verify DMX reception at 10m
- [ ] Verify DMX reception at 20m
- [ ] Note maximum working distance: _____m

### 6. Controller App Sanity Check
- [ ] From repo root run `./controller/run.sh` on macOS/Linux (or `uv pip install -r requirements.txt` + `python controller.py` manually)
- [ ] Ensure the virtual MIDI input named "LeslieCTRLs" appears inside the DAW
- [ ] Verify GUI sliders move when turning DAW knobs and that CC values forward to the selected hardware port
- [ ] Confirm the app auto-selects a Midi2DMXnow USB or "USB Single Serial" port when available

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
LED Count: 120
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
- LED strips require adequate power (120 SK6812 RGBW LEDs ≈ 7A @ full white)
- Use separate power supply for LED strips, not ESP32 power

## Performance Targets

- DMX frame rate: 30 Hz (33ms interval)
- LED refresh rate: 60 FPS
- Clock sync accuracy: <5ms
- Radio latency: <20ms typical
- Maximum receivers: Unlimited (broadcast mode)
