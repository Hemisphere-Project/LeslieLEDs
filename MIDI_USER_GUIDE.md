# MIDI Guide for Musicians

Everything you need to drive LeslieLEDs directly from a DAW, hardware controller, or the included desktop app.

## 1. Connectivity options

| Scenario | How to connect | Notes |
|----------|----------------|-------|
| **DAW on macOS/Windows** | Plug Midi2DMXnow (AtomS3) over USB-C. It enumerates as `Midi2DMXnow` on channel 1. | Best latency, powers the onboard preview strip. |
| **DIN/Serial gear** | Use the M5Core build with `USE_SERIAL_MIDI` and feed 5 V tolerant UART at 115200 baud (TX→RX0, RX→TX0). | Requires common ground; ideal for synth rigs. |
| **No DAW, just a laptop** | Launch `./controller/run.sh`. Choose the hardware port or use the virtual input `LeslieCTRLs` inside any DAW to bridge. | GUI sliders mirror inbound CCs so you can see what the DAW is doing. |

All transports expect **MIDI channel 1**. If your controller only transmits on another channel, remap in the DAW or transpose with a MIDI utility.

## 2. Quick-start recipes

### Ableton Live
1. Connect Midi2DMXnow via USB.
2. In *Preferences → Link/MIDI*, enable **Track** output for `Midi2DMXnow` (and optionally `LeslieCTRLs` if you run the controller app).
3. Create a new MIDI track, set **MIDI To** to `Midi2DMXnow`, Channel 1.
4. Add an empty MIDI clip, open the **Envelope** box, and choose `MIDI Ctrl` → the CC you need (e.g., `1 Modulation` for brightness).
5. Draw automation; as the clip plays, every receiver mirrors the preview strip.

### Logic Pro
1. Plug in Midi2DMXnow, open *Project Settings → MIDI → Sync* and make sure it is enabled as a destination.
2. Insert an External MIDI track targeting `Midi2DMXnow` channel 1.
3. Use Smart Controls or the MIDI Draw lane to automate CCs listed below.
4. Optional: run the controller app so the `LeslieCTRLs` virtual input shows up; point Logic at that port if you want to see the GUI follow your moves.

### Reaper (or anything with virtual ports)
1. Start the controller (`./controller/run.sh`). Reaper now sees two outputs: `LeslieCTRLs` (virtual) and the physical Midi2DMXnow port.
2. Send automation to `LeslieCTRLs`. The controller forwards everything to whichever hardware port you selected, and the GUI stays in sync.
3. Flip to Serial mode in the GUI when you want to talk to an M5Core device connected over USB-to-serial.

## 3. CC reference (channel 1)

| CC | DMX channel | Description | Range | Notes |
|----|-------------|-------------|-------|-------|
| 1  | 0 | Master brightness | 0–127 (scaled to 0–255) | Keep under 100 for headroom. |
| 2  | 2 | Animation speed | 0–127 | Higher = faster motion. |
| 3  | 3 | Animation control | 0–127 | Mode-dependent (segment size, waveform select, etc.). |
| 4  | 4 | Strobe rate | 0 = off, >0 = faster | Use sparingly; synced strobe shares the MeshClock. |
| 5  | 5 | Blend mode | 0–127 | 0 = hard split, 127 = smooth gradient. |
| 6  | 6 | Mirror mode | 0 None, 38 Full, 63 Split2, 88 Split3, 114 Split4 | Matches GUI dropdown. |
| 7  | 7 | Direction | 12 Forward, 38 Backward, 63 PingPong, 88 Random | Map to switches for live play. |
| 8  | 1 | Animation mode | The firmware maps `value * 10 / 128` (rounded) to the 10 animation modes: 0–12 Solid, 13–25 DualSolid, 26–38 Chase, 39–51 Dash, 52–64 Waveform, 65–76 Pulse, 77–89 Rainbow, 90–102 Sparkle, 103–115 Custom1, 116–127 Custom2. Aim for the middle of each band when mapping a hardware fader. |
| 20 | 8 | Color A Hue | 0–127 | Wraps full spectrum. |
| 21 | 9 | Color A Saturation | 0–127 | 0 = white, 127 = vivid. |
| 22 | 10 | Color A Value | 0–127 | Works alongside master brightness. |
| 23 | 11 | Color A White | 0–127 | Blends additional W channel. |
| 30 | 12 | Color B Hue | 0–127 | Secondary color for blends/dashes. |
| 31 | 13 | Color B Saturation | 0–127 | — |
| 32 | 14 | Color B Value | 0–127 | — |
| 33 | 15 | Color B White | 0–127 | — |
| 127 | 31 | Scene save mode | 0 = recall, ≥64 = arm save | Hold ≥64, press note to overwrite that slot. |

> Tip: Because MIDI tops out at 127, the firmware scales brightness to the LED engine’s 0–255 domain automatically. Set your controller faders to 0–127 so the GUI and hardware stay aligned.

## 4. Note assignments

Twenty scene slots map to MIDI notes 36–55 (C2 through D#3). Note 36 = scene 1, note 55 = scene 20.

| Note range | Function |
|------------|----------|
| 36 (C2) – 55 (D#3) | Scene 1–20 load/save. Hold = light on. **Release = blackout** (note-off blanks brightness). |

Workflow:
1. Set up a look, raise CC127 above 64 (scene save mode armed).
2. Hit the scene note you want to store. The sender's display and the controller app both flash a save banner.
3. Drop CC127 back to 0 to go back to recall mode.
4. To use a scene as a momentary stab: just hold the note as long as you want it lit; release to drop to black.

## 5. Controller app tips

- **GUI mode** – `./controller/run.sh` (uses `uv` to create `.venv` if missing). Sliders, scene buttons, port picker.
- **Headless mode** – `./controller/run.sh --headless` (optionally `--port <substring>`). No DearPyGUI, no display dependency, just the virtual MIDI bridge. Use this on production rigs driven from a DAW where the GUI would only get in the way.
- The **Output Port** combo (GUI) lists USB MIDI devices plus any `/dev/tty.*` serial adapters:
  - Labels containing `LeslieLEDs`, `M5Stack`, or `USB Single Serial` auto-select on refresh.
  - Switch between USB and serial instantly; the app forwards the virtual MIDI port to whichever path is active.
- The virtual input is always named **LeslieCTRLs**. Route your DAW track to that port to keep the GUI in sync with automation while still hitting the hardware.
- Incoming CCs update the matching slider (tagged `cc_<number>_slider`), so you always see the latest value even if it was programmed in a clip.
- Combo boxes (animation, mirror, direction) currently send but do not auto-update from DAW CCs; keep the GUI as the master when changing those parameters manually.

## 6. Best practices

- **Order of operations** – Power receivers first, then Midi2DMXnow. Look for the RGBW sweep before assuming DMX is live.
- **Velocity and scaling** – Keep CC automation between 0–120 to leave a little headroom for smoothing; the firmware clamps values anyway, but this prevents hard edges when mapping faders.
- **Scenes per song** – Dedicate one MIDI clip lane per song section with the desired scene number plus any CC sweeps.
- **Latency compensation** – ESP-NOW adds under 20 ms; if your DAW lets you offset a track by -20 ms you can align cues with audio transients perfectly.
- **Failover** – The sender re-broadcasts the full universe every 200 ms, so a momentarily dropped delta packet self-heals in ≤200 ms even if the DAW keeps streaming CCs. If a slave hears nothing for >10 s it reboots itself; the onboard LED on Atom Lite slaves goes to a slow red blink before the reboot so you can spot the offender.

## 7. Troubleshooting quick hits

| Symptom | Fix |
|---------|-----|
| No response from Midi2DMXnow | Confirm the correct transport build (`USE_USB_MIDI` vs `USE_SERIAL_MIDI`), reseat USB, check that the preview strip ran the RGBW test.
| DAW sees `LeslieCTRLs` but nothing happens | Make sure the controller app output port is set to the hardware you expect (USB or serial). |
| GUI sliders move but LEDs do not | Check MeshClock status on the sender display; if it says `Waiting`, receivers might not be powered or are out of range. |
| Scenes overwrite unexpectedly | Ensure CC127 returns to 0 after saving; automate a ramp down inside your DAW clip. |
| Colors look dull | Raise CC21/31 (saturation) and CC22/32 (value) together; also keep master brightness (CC1) below 110 to retain contrast. |

Still stuck? Run `pio device monitor` on the sender for live MIDI logs, or open an issue describing your DAW routing plus the CC/Note data that fails.
