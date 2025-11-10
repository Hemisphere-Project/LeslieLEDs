#ifndef CONFIG_H
#define CONFIG_H

// ========================================
// MIDI Configuration
// ========================================
#define MIDI_DEVICE_NAME "LeslieLEDs" // USB MIDI device name
#define MIDI_CHANNEL 1              // Single MIDI channel for all controls

// MIDI CC Mappings - Channel 1
// Global Controls (CC 0-19)
#define CC_MASTER_BRIGHTNESS 1      // Master brightness (0-127)
#define CC_ANIMATION_SPEED 2        // Animation speed multiplier
#define CC_STROBE_RATE 3            // Strobe rate (0=off)
#define CC_BLEND_MODE 4             // How colors A/B blend (0-127)
#define CC_MIRROR_MODE 5            // Mirror effect (0=off, >0=on)
#define CC_REVERSE 6                // Reverse animation direction
#define CC_SEGMENT_SIZE 7           // Size of dashing segments
#define CC_ANIMATION_MODE 8         // Animation mode selector (0-127)
// CC 9-19 reserved for future global controls

// Color A Controls (CC 20-39)
#define CC_COLOR_A_HUE 20           // Color A Hue (0-127 mapped to 0-255)
#define CC_COLOR_A_SATURATION 21    // Color A Saturation (0-127 mapped to 0-255)
#define CC_COLOR_A_VALUE 22         // Color A Value/Brightness (0-127 mapped to 0-255)
#define CC_COLOR_A_WHITE 23         // Color A White channel for RGBW (0-127)
// CC 24-39 reserved for future Color A controls

// Color B Controls (CC 40-59)
#define CC_COLOR_B_HUE 40           // Color B Hue (0-127 mapped to 0-255)
#define CC_COLOR_B_SATURATION 41    // Color B Saturation (0-127 mapped to 0-255)
#define CC_COLOR_B_VALUE 42         // Color B Value/Brightness (0-127 mapped to 0-255)
#define CC_COLOR_B_WHITE 43         // Color B White channel for RGBW (0-127)
// CC 44-59 reserved for future Color B controls

// Effects/Modulation (CC 60-79) - Reserved for future
// CC 60-79 reserved for future effects

// User/Custom (CC 80-99) - Reserved for future
// CC 80-99 reserved for user-defined controls

// System (CC 100-119) - Reserved for future
// CC 100-119 reserved for system controls

// MIDI Notes for Scene Triggers (any channel)
#define NOTE_SCENE_1 36             // C1 - Scene preset 1
#define NOTE_SCENE_2 37             // C#1
#define NOTE_SCENE_3 38             // D1
#define NOTE_SCENE_4 39             // D#1
#define NOTE_SCENE_5 40             // E1
#define NOTE_SCENE_6 41             // F1
#define NOTE_SCENE_7 42             // F#1
#define NOTE_SCENE_8 43             // G1
#define NOTE_SCENE_9 44             // G#1
#define NOTE_SCENE_10 45            // A1
#define NOTE_BLACKOUT 48            // C2 - Instant blackout

// ========================================
// LED Strip Configuration
// ========================================
#define LED_DATA_PIN 2              // GPIO pin for LED strip (AtomS3 G2)
#define LED_COUNT 300               // Number of LEDs in the strip
#define LED_TYPE SK6812             // LED strip type (SK6812 for RGBW)
#define LED_COLOR_ORDER GRB         // Color order for RGB channels
#define LED_BRIGHTNESS 128          // Default brightness (0-255)
#define LED_TARGET_FPS 60           // Target frames per second

// RMT Configuration
#define LED_RMT_CHANNEL 0           // RMT channel to use (0-7)

// ========================================
// Animation Configuration
// ========================================
#define MAX_SCENES 10               // Number of storable scenes
#define DEFAULT_ANIMATION_MODE 0    // Solid color mode

// Animation Modes
enum AnimationMode {
    ANIM_SOLID = 0,             // Single solid color
    ANIM_DUAL_SOLID,            // Two colors (A/B split or blend)
    ANIM_CHASE,                 // Chase/running lights
    ANIM_DASH,                  // Dashing/alternating segments
    ANIM_STROBE,                // Strobe effect
    ANIM_PULSE,                 // Breathing/pulsing
    ANIM_RAINBOW,               // Rainbow cycle
    ANIM_SPARKLE,               // Random sparkles
    ANIM_CUSTOM_1,              // Reserved for custom
    ANIM_CUSTOM_2,              // Reserved for custom
    ANIM_MODE_COUNT
};

// ========================================
// AtomS3 Hardware Pins
// ========================================
#define BUTTON_PIN 41               // AtomS3 built-in button
#define LED_BUILTIN 35              // AtomS3 built-in RGB LED (SK6812)

// ========================================
// Debug Configuration
// ========================================
#define DEBUG_MODE true             // Enable/disable serial debug output
#define SERIAL_BAUD_RATE 115200     // Serial monitor baud rate

// ========================================
// Display Configuration
// ========================================
#define DISPLAY_ENABLED true        // Enable/disable LCD display
#define DISPLAY_BRIGHTNESS 128      // Display brightness (0-255)
#define MIDI_LOG_LINES 4            // Number of MIDI log lines to display
#define DISPLAY_UPDATE_MS 50        // Display update interval in milliseconds

// Display colors (RGB565 format)
#define COLOR_BG 0x0000             // Black background
#define COLOR_TITLE 0xFFFF          // White title
#define COLOR_STATE_OK 0x07E0       // Green for OK state
#define COLOR_STATE_WAIT 0xFFE0     // Yellow for waiting
#define COLOR_MIDI_CC 0x07FF        // Cyan for MIDI CC messages
#define COLOR_TEXT 0xBDF7           // Light gray for text

#endif // CONFIG_H
