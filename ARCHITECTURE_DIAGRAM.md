# System Architecture

Wi-Fi free light shows: LeslieLEDs turns MIDI automation into synchronized LED strips by splitting the system into a **MIDI/DMX transmitter** (Midi2DMXnow), any number of **wireless receivers** (DMXnow2Strip), and a **Python control app** when no DAW is around. Every piece uses the same LedEngine animation library, so the preview, DMX broadcast, and strip playback stay identical.

## Roles at a Glance

- **Midi2DMXnow** – Listens to USB or Serial MIDI, runs a MeshClock peer, builds a 32-channel DMX frame and broadcasts it over ESP-NOW. Delta packets go out within ~33 ms of any change for low latency; on top of that the sender re-broadcasts the full universe every 200 ms so any slave that missed a delta resynchronises within ≤200 ms. The same DMX state drives the 120-pixel preview strip.
- **DMXnow2Strip** – Listens for the broadcast, mailboxes each frame from the Wi-Fi RX context into the main loop (heavy decode happens in the main task, not the radio callback), maps DMX channels back into LedEngine parameters and renders the same 120-pixel animation. Each boot starts with a red/green/blue/white sweep; the onboard GPIO27 LED then signals boot/ready/link-lost across the room. A task watchdog and a radio-silence self-restart cover stall-class failure modes.
- **Controller App** – `controller.py` runs as either a DearPyGUI desktop UI or `--headless` (no display dependency) MIDI bridge. Both paths expose a virtual MIDI input called `LeslieCTRLs` that a DAW can route to.
- **Shared Libraries** – `LEDengine/` (animations), `shared/leslie_protocol/` (wire-protocol constants both firmwares import) and `shared_libs/{ESPNowDMX, ESPNowMeshClock}/` (git clones of the upstream libraries) sit alongside the two PlatformIO projects.

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
2. **State Conversion** – Midi2DMXnow updates a 32-channel DMX buffer (CC1→DMX[0] master brightness, CC8→DMX[1] animation mode, CC20-23/30-33 for HSVW colors, etc.). Channel layout lives in `shared/leslie_protocol/src/leslie_protocol.h`.
3. **Wireless Broadcast** – The DMX frame goes out over ESP-NOW broadcast (no pairing): delta packets whenever channels change (rate-limited to ~30 Hz), plus a forced full-universe refresh every 200 ms so any lost delta self-heals. MeshClock ticks ride on the same radio.
4. **Mailboxed Reception** – Each receiver's Wi-Fi RX callback only memcpy's the frame into a mailbox; the main loop drains it, runs `DMXToLedEngine::applyDMXFrame`, and feeds the result to LedEngine. Heavy work never blocks the radio stack.
5. **Synchronized Rendering** – Receivers sample `meshClock.meshMillis()` and render exactly the same animation phase as the sender preview.

## Timing Guarantees

- MeshClock is symmetric (every peer broadcasts and slews forward-only toward the highest clock it hears). There is no permanent master.
- LedEngine only uses the shared clock, so `(state, meshMillis)` uniquely defines the output frame. If a packet drops, the next 200 ms full-universe refresh restores correct state.
- Receivers mark DMX as "lost" if no frame arrives for >3 s, flip the onboard LED to a slow red blink, and `ESP.restart()` if no radio activity at all for >10 s.
- Sender `ESP.restart()`s if 100+ consecutive ESP-NOW broadcasts fail (~3 s of radio wedge at 30 Hz).
- Every boot shows the RGBW diagnostic sweep before waiting for DMX so cabling mistakes are obvious — except when the previous reset was a brownout, in which case the sweep is skipped and the slave pulses purple instead (the sweep itself draws ~7 A and would re-trigger the brownout in a loop).

## Wire-format Versioning

The DMX chunk header carries a 4-bit protocol version (high nibble of byte 6, low nibble keeps the compression flag — zero overhead). A receiver running a different `PROTOCOL_VERSION` sees an unknown "compression type" via its existing branch and drops the packet, so an upgrade window produces silence-from-mismatched-peers rather than corrupted state or reboot loops. Bump `PROTOCOL_VERSION` (in `shared_libs/ESPNowDMX/src/ESPNowDMX_Common.h`) when the on-wire layout changes; coordinate a full reflash.

## Observability

Each slave broadcasts a 20-byte `HeartbeatPacket` (defined in `shared/leslie_protocol/src/leslie_protocol.h`) once per second:

```
type | version | nid[3] | resetReason | fps | uptime | freeHeap | msSinceLastFrame
```

`nid[3]` is just the last 3 bytes of the slave's MAC (Espressif's OUI prefix is redundant across a small homogeneous rig — dropping it saves bytes and makes the on-screen identifier compact).

The master's `HeartbeatCollector` keeps up to 8 slots keyed by nid, ages out slots after 30 s of silence, and the AtomS3 screen renders a small dot row above the preview (green = OK, yellow = stale >3 s, red = lost >7 s, dim grey = empty slot). Slaves naturally ignore each other's heartbeats — the ESPNowDMX receiver discards anything that isn't `PACKET_TYPE_DATA_CHUNK`, so no filtering is needed in the receive path.

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
