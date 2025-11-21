#ifndef CONFIG_H
#define CONFIG_H

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
#ifndef LED_COUNT
    #define LED_COUNT 120
#endif
#define LED_TYPE SK6812
#define LED_COLOR_ORDER GRB
#define LED_BRIGHTNESS 128
#define LED_TARGET_FPS 60
#define LED_RMT_CHANNEL 0

// ========================================
// DMX Configuration
// ========================================
#define DMX_UNIVERSE_SIZE 512
#ifndef DMX_UNIVERSE_ID
#define DMX_UNIVERSE_ID 0
#endif
#define DMX_START_ADDRESS 1

// DMX Channel Layout (must match Midi2DMXnow)
#define DMX_CH_MASTER_BRIGHTNESS 0
#define DMX_CH_ANIMATION_MODE 1
#define DMX_CH_ANIMATION_SPEED 2
#define DMX_CH_ANIMATION_CTRL 3
#define DMX_CH_STROBE_RATE 4
#define DMX_CH_BLEND_MODE 5
#define DMX_CH_MIRROR_MODE 6
#define DMX_CH_DIRECTION 7

#define DMX_CH_COLOR_A_HUE 8
#define DMX_CH_COLOR_A_SATURATION 9
#define DMX_CH_COLOR_A_VALUE 10
#define DMX_CH_COLOR_A_WHITE 11

#define DMX_CH_COLOR_B_HUE 12
#define DMX_CH_COLOR_B_SATURATION 13
#define DMX_CH_COLOR_B_VALUE 14
#define DMX_CH_COLOR_B_WHITE 15

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
