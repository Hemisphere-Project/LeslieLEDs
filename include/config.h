#ifndef CONFIG_H
#define CONFIG_H

// ========================================
// MIDI Configuration
// ========================================
#define MIDI_CHANNEL 1              // Default MIDI channel (1-16)
#define MIDI_DEVICE_NAME "LeslieLEDs" // USB MIDI device name

// ========================================
// LED Strip Configuration
// ========================================
#define LED_DATA_PIN 2              // GPIO pin for LED strip data output (AtomS3 G2)
#define LED_COUNT 60                // Number of LEDs in the strip
#define LED_TYPE WS2812B            // LED strip type (WS2812B, APA102, etc.)
#define LED_COLOR_ORDER GRB         // Color order for the LED strip
#define LED_BRIGHTNESS 128          // Default brightness (0-255)
#define LED_FPS 60                  // Target frames per second for LED updates

// ========================================
// AtomS3 Hardware Pins
// ========================================
#define BUTTON_PIN 41               // AtomS3 built-in button
#define LED_BUILTIN 35              // AtomS3 built-in RGB LED (SK6812)

// ========================================
// MIDI CC Mapping (example mappings)
// ========================================
#define CC_BRIGHTNESS 1             // CC for overall brightness
#define CC_SPEED 2                  // CC for animation speed
#define CC_MODE 3                   // CC for animation mode selection
#define CC_COLOR_HUE 4              // CC for color hue
#define CC_COLOR_SATURATION 5       // CC for color saturation
#define CC_INTENSITY 7              // CC for effect intensity

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

// ========================================
// Animation Configuration (for future use)
// ========================================
#define MAX_ANIMATION_MODES 10      // Maximum number of animation modes
#define DEFAULT_ANIMATION_MODE 0    // Default animation mode at startup
#define DEFAULT_ANIMATION_SPEED 50  // Default animation speed (0-127)

#endif // CONFIG_H
