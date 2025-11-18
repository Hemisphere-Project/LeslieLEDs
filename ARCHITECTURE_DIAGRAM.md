# System Architecture

## Data Flow Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                         MIDI Controller                           │
│                    (DAW, Hardware Controller)                     │
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
│                                                   │              │
│  ┌───────────────────────────────────────────────┘              │
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
││  DMX Receiver  ││ ││  DMX Receiver  ││          ││  DMX Receiver  ││
││  (Ch Mapping)  ││ ││  (Ch Mapping)  ││          ││  (Ch Mapping)  ││
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
  │  (300 LEDs) │      │  (300 LEDs) │              │  (300 LEDs) │
  └─────────────┘      └─────────────┘              └─────────────┘
```

## Message Flow

### 1. MIDI Input
```
MIDI Controller → [CC/Note Messages] → Midi2DMXnow
```

### 2. State Conversion
```
MIDI Handler → DMX State (32 channels)
  CC1  → DMX[0]  : Master Brightness
  CC2  → DMX[2]  : Animation Speed
  CC8  → DMX[1]  : Animation Mode
  CC20 → DMX[8]  : Color A Hue
  ...
```

### 3. Wireless Broadcast
```
DMX State → ESPNowDMX → [ESP-NOW Radio] → All Receivers
MeshClock Master → [Time Sync] → All Receivers
```

### 4. LED Rendering (Synchronized)
```
Receiver: DMX[0..15] → LED Controller State
          syncTime = meshClock.meshMillis()
          animPhase = (syncTime * speed) / 10
          render(animPhase, state) → LED Colors
```

## Key Synchronization Points

1. **Clock Master**: Midi2DMXnow maintains authoritative time
2. **Clock Slaves**: All DMXnow2Strip sync to master clock
3. **Deterministic Rendering**: Same (time, state) → Same output
4. **Result**: All strips show identical animation simultaneously

## Performance

- **DMX Rate**: ~30 Hz (33ms interval)
- **LED Refresh**: 60 FPS
- **Radio Range**: ~200m outdoors, ~50m indoors
- **Latency**: <20ms typical
- **Sync Accuracy**: <5ms between devices
