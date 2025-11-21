# LeslieLEDs

Wireless, clock-synchronized LED shows for organ-inspired rigs. Midi2DMXnow turns MIDI CC/notes into a DMX-like frame, broadcasts it over ESP-NOW, and mirrors the output on its own LED strip. DMXnow2Strip receivers listen, stay phase-locked via MeshClock, and render the exact same animation. A Python controller app provides a DAW-free UI plus a virtual MIDI bridge when you want studio automation.

## Features

- **Split architecture** – One transmitter drives any number of receivers with no Wi-Fi pairing.
- **Deterministic sync** – ESPNowMeshClock keeps every LedEngine instance on the same timeline (<5 ms skew).
- **120-pixel RGBW baseline** – Both sender (preview strip) and receivers boot with the same 120 SK6812 LEDs, GPIO2 on AtomS3 and GPIO26 on Atom Lite/M5Core.
- **RGBW boot sweep** – Every device plays a fast R→G→B→W test the moment LEDs power up.
- **Multi-transport MIDI** – USB MIDI (AtomS3), Serial MIDI (M5Core), or the controller app’s virtual port.
- **Scene workflow** – Ten presets (notes 36–45) plus a CC127 “scene save mode.”

## Repository layout

| Path | Description |
|------|-------------|
| `Midi2DMXnow/` | PlatformIO project for the transmitter + LED preview (AtomS3 / M5Core).
| `DMXnow2Strip/` | PlatformIO project for Atom Lite receivers that convert ESP-NOW DMX to LEDs.
| `LEDengine/` | Shared animation engine (standalone PlatformIO library).
| `controller/` | DearPyGUI desktop app for manual control and DAW bridging.
| `../shared_libs/ESPNowDMX` (optional) | Local checkout of the ESP-NOW DMX helper when testing forks.
| `../shared_libs/ESPNowMeshClock` (optional) | Same idea for the MeshClock helper.
| `ARCHITECTURE_DIAGRAM.md` | Current end-to-end flow and link map.
| `SETUP_CHECKLIST.md` | Flash/build/test checklist.
| `MIDI_USER_GUIDE.md` | Musician-facing MIDI+DAW guide.

Legacy references such as `Midi2Strip/` remain untouched for context only.

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
   ./run.sh                # sets up .venv via uv and launches DearPyGUI
   ```
   Use the “LeslieCTRLs” virtual MIDI port inside your DAW to drive Midi2DMXnow indirectly, or pick the hardware port directly from the GUI.
6. **Verify boot sweep + sync**
   - Receivers first, sender last. Every device flashes R→G→B→W before showing "Waiting for DMX".
   - Move a few sliders: preview strip and remote strips should mirror each other immediately.

## MIDI + DAW control

- MIDI channel 1, CC mappings straight from `config.h` (CC1 brightness, CC2 speed, CC8 mode, CC20–23 Color A HSVW, CC30–33 Color B HSVW, CC127 scene save).
- Notes 36–45 recall/save 10 presets; note 48 triggers blackout.
- `MIDI_USER_GUIDE.md` explains how to map these in Ableton/Logic/Reaper, how to use the virtual MIDI bridge, and how the GUI mirrors inbound messages.

## Additional documentation

- `ARCHITECTURE_DIAGRAM.md` – Deep dive on data flow and timing guarantees.
- `SETUP_CHECKLIST.md` – Soup-to-nuts deployment/testing list (including controller validation).
- `MIDI_USER_GUIDE.md` – Musician-focused walkthrough with CC tables, scene handling, and DAW routing tips.
- `LEDengine/README.md` – Animation API reference shared by both firmware targets.

## Development tips

- Both PlatformIO projects add `../LEDengine` and `../shared_libs` via `lib_extra_dirs`, so keep those folders siblings of the repo root.
- When patching ESPNowDMX or MeshClock locally, delete `.pio/libdeps/<env>/ESPNowDMX*` so PlatformIO reuses the sibling checkout.
- Use `pio run -t clean` after switching hardware targets to avoid stale build flags.
- The Python controller pins dependencies in `pyproject.toml` and `requirements.txt`; run `./run.sh` to create a `.venv` with `uv` and install everything.

Happy modulating! If something feels out-of-date, start with the checklist and MIDI guide—both now point to the latest boot/test behavior.
