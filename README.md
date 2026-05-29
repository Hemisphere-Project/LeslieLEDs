# LeslieLEDs

Wireless, clock-synchronized LED shows for organ-inspired rigs. Midi2DMXnow turns MIDI CC/notes into a DMX-like frame, broadcasts it over ESP-NOW, and mirrors the output on its own LED strip. DMXnow2Strip receivers listen, stay phase-locked via MeshClock, and render the exact same animation. A Python controller app provides a DAW-free UI (or a `--headless` virtual-MIDI bridge) when you want studio automation.

## Features

- **Split architecture** – One transmitter drives any number of receivers with no Wi-Fi pairing.
- **Deterministic sync** – ESPNowMeshClock is symmetric (highest-clock-wins, forward-only slew) and keeps every LedEngine instance on the same timeline (<5 ms skew).
- **Self-healing radio link** – Sender re-broadcasts the full universe every 200 ms on top of delta packets, so slaves recover from any dropped frame within ≤200 ms even under continuous CC automation.
- **120-pixel RGBW baseline** – Both sender (preview strip) and receivers boot with the same 120 SK6812 LEDs, GPIO2 on AtomS3 and GPIO26 on Atom Lite/M5Core.
- **RGBW boot sweep** – Every device plays a fast R→G→B→W test the moment LEDs power up.
- **Status beacon on Atom Lite** – The onboard GPIO27 LED shows boot/ready/link-lost/brownout state across the room, no USB needed. After a brownout reset the slave skips the bright RGBW sweep (which would re-trigger the brownout) and pulses purple.
- **Rig health on the master screen** – Each slave broadcasts a heartbeat once per second; the AtomS3 screen shows a small dot row, one coloured dot per recently-heard slave (green / yellow / red). Lets you spot a misbehaving node without USB or the GUI.
- **Watchdog + auto-restart** – Each node self-resets if `setup()` hangs, if the LED render task deadlocks, if the slave goes >10 s without any radio activity, or if the sender's ESP-NOW broadcasts fail 100 times in a row.
- **Wire-format versioning** – Packet header carries a protocol version; mismatched peers drop each other's packets cleanly instead of corrupting state during an upgrade window.
- **Multi-transport MIDI** – USB MIDI (AtomS3), Serial MIDI (M5Core), or the controller app’s virtual port.
- **Scene workflow** – Twenty presets (notes 36–55) plus a CC127 “scene save mode.”

## Repository layout

| Path | Description |
|------|-------------|
| `Midi2DMXnow/` | PlatformIO project for the transmitter + LED preview (AtomS3 / M5Core).
| `DMXnow2Strip/` | PlatformIO project for Atom Lite receivers that convert ESP-NOW DMX to LEDs.
| `LEDengine/` | Shared animation engine (standalone PlatformIO library).
| `shared/leslie_protocol/` | Header-only library with the wire-protocol constants (DMX layout, universe size) both firmwares must agree on.
| `shared_libs/ESPNowDMX/` | Local git checkout of the ESP-NOW DMX library (gitignored — clone separately, see SETUP_CHECKLIST.md).
| `shared_libs/ESPNowMeshClock/` | Local git checkout of the MeshClock library (gitignored — clone separately).
| `controller/` | DearPyGUI desktop app + `--headless` MIDI bridge for DAW production rigs.
| `ARCHITECTURE_DIAGRAM.md` | Current end-to-end flow and link map.
| `SETUP_CHECKLIST.md` | Flash/build/test checklist.
| `MIDI_USER_GUIDE.md` | Musician-facing MIDI+DAW guide.

Legacy references such as `_legacy/Midi2Strip/` remain untouched for context only.

## Quick start

1. **Install dependencies**
   - VS Code + PlatformIO
   - Python 3.10+ plus `uv` (or pip) for the controller app
2. **Build & flash Midi2DMXnow**
   ```bash
   cd Midi2DMXnow
   pio run -e m5stack_atoms3 -t upload   # USB MIDI / AtomS3
   pio run -e m5core -t upload           # Serial MIDI / M5Core
   ```
3. **Build & flash DMXnow2Strip**
   ```bash
   cd DMXnow2Strip
   pio run -e atom_lite -t upload        # Atom Lite receiver
   ```
4. **Wire LED strips** (SK6812 RGBW)
   - AtomS3 preview: GPIO2
   - Atom Lite receiver: GPIO26
   - 5 V / GND shared with strip, 120 pixels default
5. **Run the controller app (optional)**
   ```bash
   cd controller
   ./run.sh                       # GUI mode: sets up .venv via uv and launches DearPyGUI
   ./run.sh --headless            # production rigs: virtual-MIDI bridge only, no UI, no display dependency
   ./run.sh --headless --port leslie   # explicit port substring match
   ```
   Either way the bridge exposes a virtual MIDI input named **LeslieCTRLs** that your DAW can route to.
6. **Verify boot sweep + sync**
   - Receivers first, sender last. Every device flashes R→G→B→W before showing "Waiting for DMX".
   - On Atom Lite slaves, the onboard LED goes dim-red during boot, dim-green once setup completes, slow red blink if the DMX link drops, slow purple breathing if the previous reset was a brownout (skips the RGBW sweep in that case).
   - On the master's AtomS3 screen, look at the dot row above the preview: one dot per recently-heard slave. After ~1 s of normal operation all five should be green.
   - Move a few sliders: preview strip and remote strips should mirror each other immediately.

## MIDI + DAW control

- MIDI channel 1, CC mappings straight from `Midi2DMXnow/include/config.h` (CC1 brightness, CC2 speed, CC8 mode, CC20–23 Color A HSVW, CC30–33 Color B HSVW, CC127 scene save).
- Notes 36–55 recall/save 20 presets; releasing a scene note triggers blackout (hold to keep light on).
- `MIDI_USER_GUIDE.md` explains how to map these in Ableton/Logic/Reaper, how to use the virtual MIDI bridge, and how the GUI mirrors inbound messages.

## Additional documentation

- `ARCHITECTURE_DIAGRAM.md` – Deep dive on data flow and timing guarantees.
- `SETUP_CHECKLIST.md` – Soup-to-nuts deployment/testing list (including controller validation).
- `MIDI_USER_GUIDE.md` – Musician-focused walkthrough with CC tables, scene handling, and DAW routing tips.
- `LEDengine/README.md` – Animation API reference shared by both firmware targets.

## Development tips

- Both PlatformIO projects add `../LEDengine`, `../shared` (for `leslie_protocol.h`) and `../shared_libs` via `lib_extra_dirs`. `shared_libs/` is gitignored — clone `ESPNowDMX` and `ESPNowMeshClock` into it manually (see SETUP_CHECKLIST.md).
- When patching ESPNowDMX or MeshClock locally, delete `.pio/libdeps/<env>/ESPNowDMX*` so PlatformIO reuses the sibling checkout.
- Use `pio run -t clean` after switching hardware targets to avoid stale build flags.
- The Python controller pins dependencies in `pyproject.toml` and `requirements.txt`; run `./run.sh` to create a `.venv` with `uv` and install everything.

Happy modulating! If something feels out-of-date, start with the checklist and MIDI guide—both now point to the latest boot/test behavior.
