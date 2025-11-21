# System Architecture

Wi-Fi free light shows: LeslieLEDs turns MIDI automation into synchronized LED strips by splitting the system into a **MIDI/DMX transmitter** (Midi2DMXnow), any number of **wireless receivers** (DMXnow2Strip), and a **Python control app** when no DAW is around. Every piece uses the same LedEngine animation library, so the preview, DMX broadcast, and strip playback stay identical.

## Roles at a Glance

- **Midi2DMXnow** – Listens to USB or Serial MIDI, owns the MeshClock master, builds a 32-channel DMX frame, and blasts it over ESP-NOW at ~30 Hz while previewing the result on its 120-pixel RGBW strip.
- **DMXnow2Strip** – Listens for the broadcast, maps DMX channels back into LedEngine parameters, and renders the same 120-pixel animation in slave/synced mode. Each boot starts with a red/green/blue/white diagnostic sweep so wiring issues surface immediately.
- **Controller App** – DearPyGUI desktop UI with a virtual MIDI port for DAWs. It can talk straight to Midi2DMXnow over USB MIDI or forward DAW notes/CCs while keeping the GUI state in sync.
- **Shared Libraries** – `LEDengine/` (animations) plus `shared_libs/ESPNowDMX` and `shared_libs/ESPNowMeshClock` sit one directory up so both PlatformIO projects and the Python tooling use the same sources.

## Data Flow Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                         MIDI Controller                           │
│        (DAW track, Leslie Controller app, or hardware)            │
└────────────────────────────────┬─────────────────────────────────┘
                                 │
                     MIDI Messages (CC, Notes)
                                 │
                ┌────────────────▼────────────────┐
                │      USB/Serial Interface       │
                └────────────────┬────────────────┘
                                 │
┌────────────────────────────────▼───────────────────────────────┐
│                         Midi2DMXnow                             │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────────────┐   │
│  │ MIDI Handler │─▶│  DMX State   │─▶│  ESPNowDMX Sender  │   │
│  │ (USB/Serial) │  │ (32 channels)│  │  (Broadcast Mode)  │   │
│  └──────────────┘  └──────────────┘  └──────────┬─────────┘   │
│                                                   │            │
│  ┌───────────────────────────────────────────────┘            │
│  │  ESPNowMeshClock Master                                      │
│  │  (Time Synchronization)                                      │
│  └────────────────────────────────────────────┬─────────────────┤
└─────────────────────────────────────────────┬─┴─────────────────┘
                                              │
                     ESP-NOW Radio Broadcast  │
                     (DMX Frame + Clock Sync) │
                                              │
          ┌───────────────┬───────────────────┴────────────────┐
          │               │                                     │
          ▼               ▼                                     ▼
┌──────────────────┐ ┌──────────────────┐          ┌──────────────────┐
│  DMXnow2Strip    │ │  DMXnow2Strip    │   ...    │  DMXnow2Strip    │
│     Device #1    │ │     Device #2    │          │     Device #N    │
├──────────────────┤ ├──────────────────┤          ├──────────────────┤
│┌────────────────┐│ │┌────────────────┐│          │┌────────────────┐│
││ ESPNowDMX Recv ││ ││ ESPNowDMX Recv ││          ││ ESPNowDMX Recv ││
││ (Listen Mode)  ││ ││ (Listen Mode)  ││          ││ (Listen Mode)  ││
│└───────┬────────┘│ │└───────┬────────┘│          │└───────┬────────┘│
│        │         │ │        │         │          │        │         │
│┌───────▼────────┐│ │┌───────▼────────┐│          │┌───────▼────────┐│
││ DMX Adapter    ││ ││ DMX Adapter    ││          ││ DMX Adapter    ││
││ (Channel Map)  ││ ││ (Channel Map)  ││          ││ (Channel Map)  ││
│└───────┬────────┘│ │└───────┬────────┘│          │└───────┬────────┘│
│        │         │ │        │         │          │        │         │
│┌───────▼────────┐│ │┌───────▼────────┐│          │┌───────▼────────┐│
││ LED Controller ││ ││ LED Controller ││          ││ LED Controller ││
││  + MeshClock   ││ ││  + MeshClock   ││          ││  + MeshClock   ││
││   (Synced)     ││ ││   (Synced)     ││          ││   (Synced)     ││
│└───────┬────────┘│ │└───────┬────────┘│          │└───────┬────────┘│
│        │         │ │        │         │          │        │         │
│    ┌───▼───┐     │ │    ┌───▼───┐     │          │    ┌───▼───┐     │
│    │FastLED│     │ │    │FastLED│     │          │    │FastLED│     │
│    └───┬───┘     │ │    └───┬───┘     │          │    └───┬───┘     │
└────────┼─────────┘ └────────┼─────────┘          └────────┼─────────┘
         │                    │                              │
         ▼                    ▼                              ▼
  ┌─────────────┐      ┌─────────────┐              ┌─────────────┐
  │ SK6812 RGBW │      │ SK6812 RGBW │              │ SK6812 RGBW │
  │  LED Strip  │      │  LED Strip  │              │  LED Strip  │
  │ (120 LEDs)  │      │ (120 LEDs)  │              │ (120 LEDs)  │
  └─────────────┘      └─────────────┘              └─────────────┘
```

## Message Flow

1. **MIDI Input** – CC/Note data arrives from a DAW, the Leslie Controller app, or any external MIDI gear over USB or serial.
2. **State Conversion** – Midi2DMXnow updates a 32-channel DMX buffer (CC1→DMX[0] master brightness, CC8→DMX[1] animation mode, CC20-23/30-33 for HSVW colors, etc.).
3. **Wireless Broadcast** – The new DMX frame plus MeshClock ticks go out over ESP-NOW broadcast (no pairing).
4. **Synchronized Rendering** – Each receiver feeds the DMX frame through `DMXToLedEngine`, samples `meshClock.meshMillis()`, and renders exactly the same animation phase as the master preview.

## Timing Guarantees

- Midi2DMXnow is always the **MeshClock master**; receivers run as slaves and mark themselves "Waiting" if a frame is missed for >3 seconds.
- LedEngine only uses the shared clock, so `(state, meshMillis)` uniquely defines the output frame. If a packet drops, receivers keep showing the last frame until the next update.
- Every boot shows the RGBW diagnostic sweep before waiting for DMX so cabling mistakes are obvious.

## Hardware Defaults

| Device | LED Data Pin | LED Count | Notes |
|--------|--------------|-----------|-------|
| Midi2DMXnow (AtomS3) | GPIO2 | 120 | USB MIDI + onboard preview |
| Midi2DMXnow (M5Core) | GPIO26 | 120 | Serial MIDI 115200 baud |
| DMXnow2Strip (Atom Lite) | GPIO26 | 120 | ESP-NOW receiver |

Override pins/count via `build_flags`, but keep all devices aligned for identical visuals.

## Performance Snapshot

- DMX refresh: ~30 Hz (fits in ESP-NOW payload)
- LED render: Target 60 FPS using FastLED
- Sync accuracy: <5 ms between receivers once locked
- Radio range: ~50 m indoors / 200 m outdoors (line-of-sight)
- Latency: <20 ms controller-to-photon

## Related Docs

- `README.md` – Project landing page with quick start and doc map
- `SETUP_CHECKLIST.md` – End-to-end build/flash/test checklist
- `MIDI_USER_GUIDE.md` – Musician-oriented CC/note reference and DAW setup tips
- `LEDengine/README.md` – Shared animation engine API and integration notes
