#include <Arduino.h>
#include <USB.h>
#include <USBMIDI.h>
#include <M5AtomS3.h>
#include "config.h"

// ========================================
// Global Variables
// ========================================
USBMIDI MIDI;

// LED animation parameters (controlled via MIDI CC)
uint8_t currentBrightness = LED_BRIGHTNESS;
uint8_t animationSpeed = DEFAULT_ANIMATION_SPEED;
uint8_t animationMode = DEFAULT_ANIMATION_MODE;
uint8_t colorHue = 0;
uint8_t colorSaturation = 255;
uint8_t effectIntensity = 127;

// Display state
String deviceState = "INIT";
unsigned long lastDisplayUpdate = 0;

// MIDI log buffer (circular buffer)
struct MidiLogEntry {
  char text[32];
  unsigned long timestamp;
};
MidiLogEntry midiLog[MIDI_LOG_LINES];
int midiLogIndex = 0;

// ========================================
// Function Declarations
// ========================================
void setupMIDI();
void setupDisplay();
void handleMIDI();
void updateDisplay();
void addMidiLog(const char* message);
void onControlChange(byte channel, byte controller, byte value);
void updateLEDParameters();
void debugPrint(const char* message);

// ========================================
// Setup
// ========================================
void setup() {
  // Initialize M5AtomS3
  auto cfg = M5.config();
  M5.begin(cfg);
  
  // Initialize serial for debugging
  #if DEBUG_MODE
    Serial.begin(SERIAL_BAUD_RATE);
    while (!Serial && millis() < 3000); // Wait for serial or timeout after 3s
    debugPrint("=== LeslieLEDs Starting ===");
  #endif

  // Setup display
  setupDisplay();
  deviceState = "SETUP";
  updateDisplay();

  // Initialize button pin
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Setup USB MIDI
  setupMIDI();
  debugPrint("USB MIDI initialized");
  addMidiLog("MIDI Ready");

  deviceState = "READY";
  updateDisplay();

  // TODO: Initialize LED strip here when ready
  // FastLED.addLeds<LED_TYPE, LED_DATA_PIN, LED_COLOR_ORDER>(leds, LED_COUNT);
  // FastLED.setBrightness(currentBrightness);
  
  debugPrint("Setup complete - Ready to receive MIDI CC");
}

// ========================================
// Main Loop
// ========================================
void loop() {
  // Update M5 (for button handling, etc.)
  M5.update();
  
  // Handle incoming MIDI messages
  handleMIDI();
  
  // Update display periodically
  if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_MS) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  // TODO: Update LED animations here
  // This is where the LED strip animation code will go
  // Example: FastLED.show();
  
  // Small delay to prevent overwhelming the processor
  delay(1000 / LED_FPS);
}

// ========================================
// MIDI Setup
// ========================================
void setupMIDI() {
  // Initialize USB MIDI
  MIDI.begin();
  USB.begin();
}

// ========================================
// MIDI Handler - Read incoming messages
// ========================================
void handleMIDI() {
  midiEventPacket_t packet;
  
  // Read MIDI packets
  if (MIDI.readPacket(&packet)) {
    // Extract the MIDI message type from the header (Cable Index Number)
    uint8_t cin = packet.header & 0x0F;
    
    // Check if it's a Control Change message (CIN = 0x0B)
    if (cin == 0x0B) {
      byte channel = (packet.byte1 & 0x0F) + 1;  // MIDI channels are 1-16
      byte controller = packet.byte2;
      byte value = packet.byte3;
      
      onControlChange(channel, controller, value);
    }
  }
}

// ========================================
// MIDI Control Change Handler
// ========================================
void onControlChange(byte channel, byte controller, byte value) {
  // Create log message
  char logMsg[32];
  snprintf(logMsg, 32, "CH%d CC%d=%d", channel, controller, value);
  addMidiLog(logMsg);
  
  #if DEBUG_MODE
    Serial.print("MIDI CC received - Channel: ");
    Serial.print(channel);
    Serial.print(" Controller: ");
    Serial.print(controller);
    Serial.print(" Value: ");
    Serial.println(value);
  #endif

  // Map MIDI CC to LED parameters
  switch (controller) {
    case CC_BRIGHTNESS:
      currentBrightness = map(value, 0, 127, 0, 255);
      debugPrint("Brightness updated");
      // TODO: FastLED.setBrightness(currentBrightness);
      break;
      
    case CC_SPEED:
      animationSpeed = value;
      debugPrint("Animation speed updated");
      break;
      
    case CC_MODE:
      animationMode = value % MAX_ANIMATION_MODES;
      debugPrint("Animation mode changed");
      break;
      
    case CC_COLOR_HUE:
      colorHue = map(value, 0, 127, 0, 255);
      debugPrint("Color hue updated");
      break;
      
    case CC_COLOR_SATURATION:
      colorSaturation = map(value, 0, 127, 0, 255);
      debugPrint("Color saturation updated");
      break;
      
    case CC_INTENSITY:
      effectIntensity = value;
      debugPrint("Effect intensity updated");
      break;
      
    default:
      // Unknown CC - can be logged or ignored
      break;
  }
  
  updateLEDParameters();
}

// ========================================
// Update LED Parameters
// ========================================
void updateLEDParameters() {
  // This function will be used to apply the MIDI CC changes to LED animations
  // Placeholder for future LED control code
  
  // Example of what will go here:
  // - Update animation speed
  // - Change color palette
  // - Switch animation modes
  // - Adjust brightness in real-time
}

// ========================================
// Debug Helper
// ========================================
void debugPrint(const char* message) {
  #if DEBUG_MODE
    Serial.println(message);
  #endif
}

// ========================================
// Display Setup
// ========================================
void setupDisplay() {
  #if DISPLAY_ENABLED
    // Set display brightness
    M5.Display.setBrightness(DISPLAY_BRIGHTNESS);
    
    // Clear screen
    M5.Display.fillScreen(COLOR_BG);
    
    // Initialize MIDI log
    for (int i = 0; i < MIDI_LOG_LINES; i++) {
      midiLog[i].text[0] = '\0';
      midiLog[i].timestamp = 0;
    }
    
    // Draw initial UI
    M5.Display.setTextColor(COLOR_TITLE, COLOR_BG);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(2, 2);
    M5.Display.println("LeslieLEDs");
  #endif
}

// ========================================
// Update Display
// ========================================
void updateDisplay() {
  #if DISPLAY_ENABLED
    // Get display dimensions
    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();
    
    // Title area (top 12px)
    M5.Display.fillRect(0, 0, w, 12, COLOR_BG);
    M5.Display.setTextColor(COLOR_TITLE, COLOR_BG);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(2, 2);
    M5.Display.print("LeslieLEDs");
    
    // State area (line 2, 12-24px)
    M5.Display.fillRect(0, 12, w, 12, COLOR_BG);
    uint16_t stateColor = deviceState == "READY" ? COLOR_STATE_OK : COLOR_STATE_WAIT;
    M5.Display.setTextColor(stateColor, COLOR_BG);
    M5.Display.setCursor(2, 14);
    M5.Display.print(deviceState);
    M5.Display.print(" M:");
    M5.Display.print(animationMode);
    M5.Display.print(" B:");
    M5.Display.print(currentBrightness);
    
    // Separator line
    M5.Display.drawFastHLine(0, 25, w, COLOR_TEXT);
    
    // MIDI Log area (remaining space)
    int logStartY = 28;
    int lineHeight = 10;
    
    M5.Display.fillRect(0, logStartY, w, h - logStartY, COLOR_BG);
    M5.Display.setTextColor(COLOR_MIDI_CC, COLOR_BG);
    M5.Display.setTextSize(1);
    
    // Display most recent MIDI messages (reverse order)
    int displayLine = 0;
    for (int i = 0; i < MIDI_LOG_LINES && displayLine < MIDI_LOG_LINES; i++) {
      int logIdx = (midiLogIndex - 1 - i + MIDI_LOG_LINES) % MIDI_LOG_LINES;
      if (midiLog[logIdx].text[0] != '\0') {
        int y = logStartY + (displayLine * lineHeight);
        if (y + lineHeight <= h) {
          M5.Display.setCursor(2, y);
          M5.Display.print(midiLog[logIdx].text);
          displayLine++;
        }
      }
    }
  #endif
}

// ========================================
// Add MIDI Log Entry
// ========================================
void addMidiLog(const char* message) {
  #if DISPLAY_ENABLED
    strncpy(midiLog[midiLogIndex].text, message, 31);
    midiLog[midiLogIndex].text[31] = '\0';
    midiLog[midiLogIndex].timestamp = millis();
    midiLogIndex = (midiLogIndex + 1) % MIDI_LOG_LINES;
  #endif
}
