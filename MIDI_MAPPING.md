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
- **E1 (40)** - Scene 5: White waveform (sine)
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
| 1 | Master Brightness | 0-127 | Overall strip brightness (mapped to 0-255) |
| 2 | Animation Speed | 0-127 | Speed multiplier for animations |
| 3 | Animation Ctrl | 0-127 | Animation-specific control (usage varies by mode) |
| 4 | Strobe Rate | 0-127 | Strobe overlay: 0=off, 1-127=faster (overlays on any animation) |
| 5 | Blend Mode | 0-127 | Color mixing: 0=hard split, 127=smooth gradient |
| 6 | Mirror Mode | 0-127 | 0-25=none, 26-50=full, 51-75=split2, 76-100=split3, 101-127=split4 |
| 7 | Direction | 0-127 | 0-25=forward, 26-50=backward, 51-75=pingpong, 76-100=random, 101-127=reserved |
| 8 | Animation Mode | 0-127 | 0=blackout, 1-9=solid, 10-19=dual, 20-29=chase, 30-39=dash, 40-49=waveform, etc. |
| 9-19 | *Reserved* | - | Future global controls |

**Animation Ctrl (CC3) Usage by Mode:**
- **CHASE/DASH:** Segment size (1-75 LEDs)
- **WAVEFORM:** Waveform selector (0-31=sine, 32-63=triangle, 64-95=square, 96-127=sawtooth)
- **Other modes:** Reserved for future use

---

### Color A Controls (CC 20-29)

| CC# | Parameter | Range | Description |
|-----|-----------|-------|-------------|
| 20 | Hue A | 0-127 | Color A hue (mapped to 0-255) |
| 21 | Saturation A | 0-127 | Color A saturation (0=white, 127=full color) |
| 22 | Value A | 0-127 | Color A brightness/intensity |
| 23 | White A | 0-127 | Color A RGBW white channel intensity |
| 24-29 | *Reserved* | - | Future Color A controls |

---

### Color B Controls (CC 30-39)

| CC# | Parameter | Range | Description |
|-----|-----------|-------|-------------|
| 30 | Hue B | 0-127 | Color B hue (mapped to 0-255) |
| 31 | Saturation B | 0-127 | Color B saturation (0=white, 127=full color) |
| 32 | Value B | 0-127 | Color B brightness/intensity |
| 33 | White B | 0-127 | Color B RGBW white channel intensity |
| 34-39 | *Reserved* | - | Future Color B controls |

---

### Future Expansion

| CC Range | Reserved For |
|----------|--------------|
| 40-79 | Effects/Modulation |
| 80-99 | User/Custom controls |
| 100-119 | System controls |
| 120-126 | MIDI standard (avoid) |
| **127** | **Scene Save Mode** (0-37=load, 38-127=save) |

---

## Animation Modes

Modes can be selected via **CC8** or by triggering scene presets:

| CC8 Value | Animation Mode | Description |
|-----------|----------------|-------------|
| 0 | **BLACKOUT** | All LEDs off |
| 1-9 | **SOLID** | Single solid color (Color A) |
| 10-19 | **DUAL_SOLID** | Two colors split or blended (A/B) |
| 20-29 | **CHASE** | Running lights chasing down the strip |
| 30-39 | **DASH** | Alternating dashed segments |
| 40-49 | **WAVEFORM** | Waveform pulse (sine/triangle/square/sawtooth - select via CC3) |
| 50-59 | **PULSE** | Simple breathing/pulsing effect |
| 60-69 | **RAINBOW** | Rainbow cycle animation |
| 70-79 | **SPARKLE** | Random sparkle effect |
| 80-127 | **CUSTOM** | Reserved for custom animations |

**Note:** Strobe is now a **global overlay effect** (CC4), not a dedicated animation mode. You can apply strobe to any animation!

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
4. **Saving scenes**: Set CC127 to 127, then trigger note to save current state

### Scene Save/Load
- **Load a scene**: CC127 ≤ 37 (default), then trigger note C1-A1
- **Save to a scene**: 
  1. Set CC127 ≥ 38 (enable save mode - prevents knob drift at zero)
  2. Trigger note C1-A1 to save current state to that scene
  3. Set CC127 back to 0 (return to load mode)
  4. **Display shows** "SCENE X SAVED" in green, LEDs flash green briefly
  5. When loading: **Display shows** "SCENE X LOADED" in blue

**Tip**: Map CC127 to a toggle button on your controller for quick scene saving during soundcheck!

### Live Performance Tips
- **Pre-build scenes**: Set up your 10 scenes before the show
- **Automate fades**: Use CC envelopes for smooth transitions
- **Strobe sparingly**: CC4 strobe overlays on any animation - use wisely!
- **Mirror modes**: 5 levels for different stage setups (full/split2/split3/split4)
- **Direction modes**: Forward/backward/pingpong/random for animation variation
- **Animation Ctrl (CC3)**: Changes function per animation mode (segment size for DASH/CHASE, waveform for WAVEFORM)
- **Blend mode**: Smooth color transitions between songs

### Example Workflow
```
Song Intro:    Trigger Scene 1 (Red solid, low brightness)
Build-up:      Automate CC2 (speed) ramping up
               Automate CC1 (brightness) increasing
Drop:          Trigger Scene 4 (Dash mode, full brightness)
               Add CC4 strobe overlay for intensity
Breakdown:     Trigger Scene 5 (Waveform mode, Color A = white)
               CC3 to switch between sine/square waves
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
- White channel (CC23/CC33) adds pure white light
- Saturation (CC21/CC31) at 0 gives pure white through RGB

**Strobe not working?**
- Strobe is now a global overlay (CC4), not an animation mode
- Set CC4 > 0 to add strobe effect on top of any animation
- Higher CC4 values = faster strobe rate

---

## Advanced: Creating Custom Scenes

You can save the current lighting state to any scene slot (1-10) using the **Scene Save Mode**:

### How to Save a Scene
1. **Set up your desired look**: Adjust all parameters via MIDI CC (colors, speed, mode, etc.)
2. **Enable save mode**: Send CC127 = 127
3. **Trigger the scene note**: Send the note for the scene you want to overwrite (C1-A1)
4. **Confirm**: LEDs flash green briefly, scene is saved!
5. **Disable save mode**: Send CC127 = 0 (return to normal load mode)

### Example: Saving to Scene 6 (F1)
```
1. CC20 = 85  (Purple hue for Color A)
2. CC2 = 100  (Fast animation speed)
3. CC8 = 25   (Chase mode)
4. CC127 = 127  (Enable save mode)
5. Note F1 (41) ON  (Save to scene 6)
   → Display shows "SCENE 6 SAVED" in green 🟢
   → LEDs flash green briefly ✅
6. CC127 = 0  (Disable save mode)
```

### Ableton Live Workflow
- Map CC127 to a **toggle button** on your controller
- During soundcheck, dial in looks and save them to scenes 6-10
- During performance, keep CC127 off and just trigger scenes normally
- **Visual confirmation**: AtomS3 display shows full-screen scene number with SAVED/LOADED status

**Note**: Saved scenes persist until device restart. Scene 1-5 are factory defaults and will reset on reboot.
