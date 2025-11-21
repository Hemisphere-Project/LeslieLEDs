# DMX Multi-Fixture Implementation

## Overview
LeslieLEDs now supports multiple DMX fixtures with configurable pixel mapping. Each fixture uses 31 DMX channels, allowing up to 16 fixtures (496 channels total).

## Fixture Layout
- **Channels per fixture:** 31
- **Maximum fixtures:** 16
- **Total channels used:** 496 (of 512 available)

### Channel Spacing
- Fixture 1: Channels 1-31
- Fixture 2: Channels 32-62
- Fixture 3: Channels 63-93
- etc...

### Per-Fixture Channel Map (first 6 of 31 channels)
Each fixture uses the following channel layout:
- **Channel 0 (+0):** Red (0-255)
- **Channel 1 (+1):** Green (0-255)
- **Channel 2 (+2):** Blue (0-255)
- **Channel 3 (+3):** White (0-255)
- **Channel 4 (+4):** Master/Dimmer (0-255)
- **Channels 5-30 (+5-30):** Reserved for future expansion

## Pixel Mapping
The `g_dmxPixelMap[]` array defines which LED strip pixel each DMX fixture controls.

### Default Mapping
```cpp
uint16_t g_dmxPixelMap[DMX_MAX_FIXTURES] = {
    0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150
};
```

**Example:**
- Fixture 1 (DMX ch 1-31) → Controls LED Pixel 0
- Fixture 2 (DMX ch 32-62) → Controls LED Pixel 10
- Fixture 3 (DMX ch 63-93) → Controls LED Pixel 20
- etc...

### Active Fixtures
```cpp
uint8_t g_dmxActiveFixtures = 4;  // Number of fixtures to update (1-16)
```

## Configuration (config.h)
```cpp
#define DMX_CHANNELS_PER_FIXTURE 31
#define DMX_MAX_FIXTURES 16
#define DMX_FIXTURE_START 1

extern uint16_t g_dmxPixelMap[DMX_MAX_FIXTURES];
```

## Implementation Details

### Fixture Address Calculation
For fixture N (0-indexed):
```cpp
uint16_t dmxStart = (fixtureNum * DMX_CHANNELS_PER_FIXTURE) + DMX_FIXTURE_START;
```

### Update Loop
The `updateFixture()` method iterates through all active fixtures:
1. Gets pixel index from `g_dmxPixelMap[fixtureNum]`
2. Calculates DMX start channel for the fixture
3. Reads color data for that pixel
4. Applies master brightness and strobe
5. Sets RGBW + Master channels in DMX buffer

### Serial Debug Output
On initialization, the system prints:
```
DMX initialized: LXESP32DMX on M5Module-DMX512 (TX=13, EN=12)
DMX channels: 512
Multi-fixture mode: 4 fixtures @ 31 channels each
Pixel mapping:
  Fixture 1: Pixel 0 -> DMX channels 1-31
  Fixture 2: Pixel 10 -> DMX channels 32-62
  Fixture 3: Pixel 20 -> DMX channels 63-93
  Fixture 4: Pixel 30 -> DMX channels 94-124
```

## Future Enhancements

### Dynamic Pixel Mapping via MIDI
Future MIDI CC mappings could allow:
- Setting active fixture count
- Configuring pixel-to-fixture mapping
- Enabling/disabling individual fixtures

### Per-Pixel Color Reading
Currently all fixtures use the same color (ColorA). Future enhancement could read individual pixel colors from the LED strip buffer.

### Expanded Channel Usage
The 25 reserved channels (5-30) per fixture could be used for:
- Pan/Tilt (for moving heads)
- Effect selection
- Speed/Rate control
- Pattern parameters
- Zoom/Focus
- Color macros

## Testing
Both M5Core and M5Stack AtomS3 environments compile successfully with multi-fixture support:
```bash
pio run -e m5core          # SUCCESS
pio run -e m5stack_atoms3  # SUCCESS
```

## Hardware Requirements
- **M5Core:** M5Module-DMX512 (TX=13, RX=35, EN=12)
  - Enable pin (GPIO12) MUST be HIGH for RS485 transceiver
- **M5Stack AtomS3:** M5Unit-DMX512 on PortC (TX=5)
  - No enable pin required

## Library
Uses LXESP32DMX v3.0.0 (claudeheintz/LXESP32DMX) for reliable DMX output.
