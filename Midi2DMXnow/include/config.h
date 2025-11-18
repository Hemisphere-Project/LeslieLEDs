#ifndef CONFIG_H
#define CONFIG_H

// ========================================
// Platform Detection & Configuration
// ========================================
#if defined(PLATFORM_ATOMS3)
    #define PLATFORM_NAME "AtomS3"
    #define BUTTON_PIN 41
    #define HAS_SMALL_DISPLAY true
    #ifndef LED_DATA_PIN
        #define LED_DATA_PIN 2
    #endif
#elif defined(PLATFORM_M5CORE)
    #define PLATFORM_NAME "M5Core"
    #define BUTTON_PIN 39
    #define HAS_SMALL_DISPLAY true
    #ifndef LED_DATA_PIN
        #define LED_DATA_PIN 26
    #endif
#else
    #error "Platform not defined! Use -DPLATFORM_ATOMS3 or -DPLATFORM_M5CORE"
#endif

#ifndef LED_COUNT
    #define LED_COUNT 300
#endif

// ========================================
// Communication Mode Configuration
// ========================================
#if defined(USE_USB_MIDI)
    #define COMM_MODE "USB MIDI"
    #define MIDI_VIA_SERIAL false
#elif defined(USE_SERIAL_MIDI)
    #define COMM_MODE "Serial MIDI"
    #define MIDI_VIA_SERIAL true
    #define SERIAL_MIDI_BAUD 115200
#else
    #error "Communication mode not defined! Use -DUSE_USB_MIDI or -DUSE_SERIAL_MIDI"
#endif

// ========================================
// MIDI Configuration
// ========================================
#define MIDI_DEVICE_NAME "Midi2DMXnow"
#define MIDI_CHANNEL 1

// MIDI CC Mappings
#define CC_MASTER_BRIGHTNESS 1
#define CC_ANIMATION_SPEED 2
#define CC_ANIMATION_CTRL 3
#define CC_STROBE_RATE 4
#define CC_BLEND_MODE 5
#define CC_MIRROR_MODE 6
#define CC_DIRECTION 7
#define CC_ANIMATION_MODE 8

#define CC_COLOR_A_HUE 20
#define CC_COLOR_A_SATURATION 21
#define CC_COLOR_A_VALUE 22
#define CC_COLOR_A_WHITE 23

#define CC_COLOR_B_HUE 30
#define CC_COLOR_B_SATURATION 31
#define CC_COLOR_B_VALUE 32
#define CC_COLOR_B_WHITE 33

#define CC_SCENE_SAVE_MODE 127

// MIDI Notes for Scene Triggers
#define NOTE_SCENE_1 36
#define NOTE_SCENE_2 37
#define NOTE_SCENE_3 38
#define NOTE_SCENE_4 39
#define NOTE_SCENE_5 40
#define NOTE_SCENE_6 41
#define NOTE_SCENE_7 42
#define NOTE_SCENE_8 43
#define NOTE_SCENE_9 44
#define NOTE_SCENE_10 45
#define NOTE_BLACKOUT 48

// ========================================
// DMX Configuration
// ========================================
#define DMX_UNIVERSE_SIZE 512
#define DMX_START_ADDRESS 1
#ifndef DMX_UNIVERSE_ID
#define DMX_UNIVERSE_ID 0
#endif

// DMX Channel Layout (32 channels total)
#define DMX_CH_MASTER_BRIGHTNESS 0    // 0-255
#define DMX_CH_ANIMATION_MODE 1       // 0-255 (0-25 per mode)
#define DMX_CH_ANIMATION_SPEED 2      // 0-255
#define DMX_CH_ANIMATION_CTRL 3       // 0-255
#define DMX_CH_STROBE_RATE 4          // 0-255
#define DMX_CH_BLEND_MODE 5           // 0-255
#define DMX_CH_MIRROR_MODE 6          // 0-255
#define DMX_CH_DIRECTION 7            // 0-255

#define DMX_CH_COLOR_A_HUE 8          // 0-255
#define DMX_CH_COLOR_A_SATURATION 9   // 0-255
#define DMX_CH_COLOR_A_VALUE 10       // 0-255
#define DMX_CH_COLOR_A_WHITE 11       // 0-255

#define DMX_CH_COLOR_B_HUE 12         // 0-255
#define DMX_CH_COLOR_B_SATURATION 13  // 0-255
#define DMX_CH_COLOR_B_VALUE 14       // 0-255
#define DMX_CH_COLOR_B_WHITE 15       // 0-255

// Channels 16-31 reserved for future use

// ========================================
// Scene Configuration
// ========================================
#define MAX_SCENES 10

// ========================================
// Debug Configuration
// ========================================
#define DEBUG_MODE true
#define SERIAL_BAUD_RATE 115200

// ========================================
// Display Configuration
// ========================================
#define DISPLAY_ENABLED true
#define DISPLAY_BRIGHTNESS 128
#define DISPLAY_UPDATE_MS 50
#define MIDI_LOG_LINES 8

// Display colors (RGB565 format)
#define COLOR_BG 0x0000
#define COLOR_TITLE 0xFFFF
#define COLOR_STATE_OK 0x07E0
#define COLOR_STATE_WAIT 0xFFE0
#define COLOR_MIDI_CC 0x07FF
#define COLOR_TEXT 0xBDF7

#endif // CONFIG_H
