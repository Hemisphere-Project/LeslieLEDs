#ifndef CONFIG_H
#define CONFIG_H

// Shared wire-protocol constants (DMX universe + channel layout +
// LED_COUNT default). Must match the sender firmware byte-for-byte.
#include <leslie_protocol.h>

// ========================================
// Platform Detection & Configuration
// ========================================
#if defined(PLATFORM_ATOM_LITE)
    #define PLATFORM_NAME "Atom Lite"
    #ifndef LED_DATA_PIN
        #define LED_DATA_PIN 26
    #endif
    #define BUTTON_PIN 39
    #define LED_BUILTIN 27
    #define HAS_SMALL_DISPLAY false
#else
    #error "Platform not defined! Use -DPLATFORM_ATOM_LITE"
#endif

// ========================================
// LED Strip Configuration
// ========================================
#define LED_TYPE SK6812
#define LED_COLOR_ORDER GRB
#define LED_BRIGHTNESS 10
#define LED_TARGET_FPS 60
#define LED_RMT_CHANNEL 0

// DMX universe size + channel layout are defined in leslie_protocol.h
// (shared with the sender firmware).

// ========================================
// Debug Configuration
// ========================================
#define DEBUG_MODE true
#define SERIAL_BAUD_RATE 115200

// ========================================
// Display Configuration
// ========================================
#define DISPLAY_ENABLED HAS_SMALL_DISPLAY
#define DISPLAY_BRIGHTNESS 128
#define DISPLAY_UPDATE_MS 50

#endif // CONFIG_H
