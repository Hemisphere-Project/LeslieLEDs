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

// ---------------------------------------------------------------------
// Slave → Master heartbeat
//
// Each receiver broadcasts a HeartbeatPacket every LESLIE_HEARTBEAT_PERIOD_MS
// so the master (and the controller GUI, later) can render a per-node
// health view: who is alive, when did each last receive a DMX frame,
// what was their previous reset reason, free heap, FPS.
//
// Type byte == PACKET_TYPE_HEARTBEAT (0x10) — distinct from the DMX
// chunk type (0x01) so the existing ESPNowDMX receiver code drops these
// naturally (its check is `data[0] != PACKET_TYPE_DATA_CHUNK`).
//
// nid[3] = last 3 bytes of the sender's MAC. Espressif assigns the
// high 3 bytes from a finite pool of OUIs that are common across all
// ESP32 chips in the same family, so they convey no information for
// a small rig — the low 3 bytes are the per-chip serial and are
// unique in practice. Saves wire bytes and makes the on-screen
// per-node identifier compact (e.g. "A4 12 0F" fits one line).
//
// Layout: 20 bytes, 4-byte aligned. pack(1) is explicit so it stays
// stable across compilers / toolchains.
// ---------------------------------------------------------------------
#define PACKET_TYPE_HEARTBEAT       0x10
#define LESLIE_HEARTBEAT_VERSION    0x01
#define LESLIE_HEARTBEAT_PERIOD_MS  1000UL

#ifdef __cplusplus
#include <stdint.h>

#pragma pack(push, 1)
struct HeartbeatPacket {
    uint8_t  type;              // = PACKET_TYPE_HEARTBEAT
    uint8_t  version;           // = LESLIE_HEARTBEAT_VERSION
    uint8_t  nid[3];            // last 3 bytes of sender MAC (unique per chip)
    uint8_t  lastResetReason;   // esp_reset_reason() recorded at boot
    uint8_t  fps;               // current LED engine FPS
    uint8_t  reserved;          // pad to 4-byte alignment for the uint32 fields
    uint32_t uptimeSec;         // millis() / 1000 at send time
    uint32_t freeHeap;          // ESP.getFreeHeap()
    uint32_t msSinceLastFrame;  // ms since the slave last applied a DMX frame; UINT32_MAX = never
};
#pragma pack(pop)

static_assert(sizeof(HeartbeatPacket) == 20, "HeartbeatPacket layout drifted");
#endif // __cplusplus

#endif // LESLIE_PROTOCOL_H
