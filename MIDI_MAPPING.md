# LeslieLEDs MIDI Mapping Guide

## Quick Start for Ableton Live

### MIDI Channel
**All controls use MIDI Channel 1** - Single track in Ableton!

### Scene Triggers (Notes)
Map these notes to trigger preset scenes:
- **C1 (36)** - Scene 1: Red solid
- **C#1 (37)** - Scene 2: Blue solid
- **D1 (38)** - Scene 3: Rainbow chase
- **D#1 (39)** - Scene 4: Red/Blue dash
- **E1 (40)** - Scene 5: White strobe
- **F1 (41)** - Scene 6: User preset
- **F#1 (42)** - Scene 7: User preset
- **G1 (43)** - Scene 8: User preset
- **G#1 (44)** - Scene 9: User preset
- **A1 (45)** - Scene 10: User preset
- **C2 (48)** - **BLACKOUT** (instant off)

---

## CC Mapping (All on Channel 1)

### Global Controls (CC 0-19)

| CC# | Parameter | Range | Description |
|-----|-----------|-------|-------------|
| 1 | Master Brightness | 0-127 | Overall strip brightness |
| 2 | Animation Speed | 0-127 | Speed multiplier for animations |
| 3 | Strobe Rate | 0-127 | 0=off, higher=faster strobe |
| 4 | Blend Mode | 0-127 | How colors A/B mix (0=split, 127=gradient) |
| 5 | Mirror Mode | 0-127 | <64=off, ≥64=mirror effect |
| 6 | Reverse | 0-127 | <64=normal, ≥64=reverse direction |
| 7 | Segment Size | 0-127 | Size of dashing segments (1-75 LEDs) |
| 8 | Animation Mode | 0-127 | Direct mode select (0-12=mode0, 13-25=mode1, etc.) |
| 9-19 | *Reserved* | - | Future global controls |

---

### Color A Controls (CC 20-39)

| CC# | Parameter | Range | Description |
|-----|-----------|-------|-------------|
| 20 | Hue A | 0-127 | Color A hue (mapped to 0-255) |
| 21 | Saturation A | 0-127 | Color A saturation (0=white, 127=full color) |
| 22 | Value A | 0-127 | Color A brightness/intensity |
| 23 | White A | 0-127 | Color A RGBW white channel intensity |
| 24-39 | *Reserved* | - | Future Color A controls |

---

### Color B Controls (CC 40-59)

| CC# | Parameter | Range | Description |
|-----|-----------|-------|-------------|
| 40 | Hue B | 0-127 | Color B hue (mapped to 0-255) |
| 41 | Saturation B | 0-127 | Color B saturation (0=white, 127=full color) |
| 42 | Value B | 0-127 | Color B brightness/intensity |
| 43 | White B | 0-127 | Color B RGBW white channel intensity |
| 44-59 | *Reserved* | - | Future Color B controls |

---

### Future Expansion

| CC Range | Reserved For |
|----------|--------------|
| 60-79 | Effects/Modulation |
| 80-99 | User/Custom controls |
| 100-119 | System controls |
| 120-127 | MIDI standard (avoid) |

---

## Animation Modes

Modes can be selected via scenes or by setting via custom implementation:

0. **SOLID** - Single solid color (Color A)
1. **DUAL_SOLID** - Two colors split or blended (A/B)
2. **CHASE** - Running lights chasing down the strip
3. **DASH** - Alternating dashed segments
4. **STROBE** - Strobe effect (set rate with CC3)
5. **PULSE** - Breathing/pulsing effect
6. **RAINBOW** - Rainbow cycle animation
7. **SPARKLE** - Random sparkle effect
8-9. **CUSTOM** - Reserved for custom animations

---

## Ableton Live Setup Tips

### Basic Setup
1. Create **one MIDI track** for LeslieLEDs
2. Set output to LeslieLEDs USB MIDI device, Channel 1
3. Use Clip Envelopes to automate CC values over time
4. **Pro Tip**: Group CCs by 20s for easy organization (20s=ColorA, 40s=ColorB)

### Scene-Based Workflow
1. Map notes C1-A1 to clips on a MIDI track
2. Trigger scenes with your controller or arrangement
3. Use C2 for emergency blackout

### Live Performance Tips
- **Pre-build scenes**: Set up your 10 scenes before the show
- **Automate fades**: Use CC envelopes for smooth transitions
- **Strobe sparingly**: High strobe rates are intense!
- **Mirror mode**: Great for symmetric stage setups
- **Blend mode**: Smooth color transitions between songs

### Example Workflow
```
Song Intro:    Trigger Scene 1 (Red solid, low brightness)
Build-up:      Automate CC2 (speed) ramping up
               Automate CC1 (brightness) increasing
Drop:          Trigger Scene 4 (Dash mode, full brightness)
Breakdown:     Trigger Scene 6 (Pulse mode, Color A = blue)
Outro:         Fade CC1 to 0 over 8 bars
```

---

## Technical Notes

- **FPS Target**: 30-60 FPS (monitor display for actual FPS)
- **RMT Driver**: Non-blocking FastLED with RMT for smooth MIDI response
- **LED Count**: 300 SK6812 RGBW pixels
- **Response Time**: <10ms typical MIDI latency

---

## Troubleshooting

**LEDs not responding?**
- Check USB connection
- Verify MIDI channel routing in Ableton
- Try sending a scene trigger (C1) to test

**Choppy animations?**
- Reduce animation speed (CC2)
- Check FPS on display (should be 30+)
- Reduce number of active automation lanes

**Colors look wrong?**
- SK6812 uses GRB color order (handled automatically)
- White channel (CC13) adds pure white light
- Saturation (CC11) at 0 gives pure white through RGB

---

## Advanced: Creating Custom Scenes

While performing, you can "save" the current settings to a scene:
1. Adjust all parameters to desired state via MIDI CC
2. (Future feature: scene saving via button combo)

Currently, scenes 6-10 are user-configurable in code.
