#ifndef LESLIE_PROTOCOL_H
#define LESLIE_PROTOCOL_H

// =====================================================================
// LeslieLEDs shared wire-protocol constants.
//
// Anything that MUST match byte-for-byte between Midi2DMXnow (sender)
// and DMXnow2Strip (receivers) lives here. Sender-only or receiver-only
// concerns (MIDI CC mappings, NVS keys, display config, etc.) stay in
// each project's local config.h.
//
// Bumping anything here is a coordinated change: every node has to be
// reflashed together or they'll drift apart silently.
// =====================================================================

// ---------------------------------------------------------------------
// DMX universe sizing
// ---------------------------------------------------------------------
#define DMX_UNIVERSE_SIZE 512
#define DMX_START_ADDRESS 1

#ifndef DMX_UNIVERSE_ID
#define DMX_UNIVERSE_ID 0
#endif

// ---------------------------------------------------------------------
// DMX channel layout (32 channels used; rest reserved)
// Both projects must agree on every offset here.
// ---------------------------------------------------------------------
#define DMX_CH_MASTER_BRIGHTNESS  0   // 0-255
#define DMX_CH_ANIMATION_MODE     1   // 0-255 (decoded as value/25 -> mode index)
#define DMX_CH_ANIMATION_SPEED    2   // 0-255
#define DMX_CH_ANIMATION_CTRL     3   // 0-255 (mode-dependent)
#define DMX_CH_STROBE_RATE        4   // 0-255 (0 = off)
#define DMX_CH_BLEND_MODE         5   // 0-255
#define DMX_CH_MIRROR_MODE        6   // 0-255 (5 bands -> MIRROR_NONE..MIRROR_SPLIT4)
#define DMX_CH_DIRECTION          7   // 0-255 (4 bands -> DIR_FORWARD..DIR_RANDOM)

#define DMX_CH_COLOR_A_HUE        8   // 0-255
#define DMX_CH_COLOR_A_SATURATION 9   // 0-255
#define DMX_CH_COLOR_A_VALUE      10  // 0-255
#define DMX_CH_COLOR_A_WHITE      11  // 0-255

#define DMX_CH_COLOR_B_HUE        12  // 0-255
#define DMX_CH_COLOR_B_SATURATION 13  // 0-255
#define DMX_CH_COLOR_B_VALUE      14  // 0-255
#define DMX_CH_COLOR_B_WHITE      15  // 0-255

// Channels 16-31 reserved for future use.

// ---------------------------------------------------------------------
// LED strip defaults
// Both sender preview and receivers boot with this count unless their
// build_flags override LED_COUNT.
// ---------------------------------------------------------------------
#ifndef LED_COUNT
#define LED_COUNT 120
#endif

#endif // LESLIE_PROTOCOL_H
