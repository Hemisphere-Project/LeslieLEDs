#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"
#include "led_controller.h"
#include "display_handler.h"

// Platform-specific MIDI handler
#if MIDI_VIA_SERIAL
  #include "serial_midi_handler.h"
  SerialMIDIHandler midiHandler;
#else
  #include "midi_handler.h"
  MIDIHandler midiHandler;
#endif

// ========================================
// Global Objects
// ========================================
LEDController ledController;
DisplayHandler displayHandler;

// ========================================
// Setup
// ========================================
void setup() {
  // Initialize M5 (GPIO 26 doesn't conflict with any M5Core peripherals)
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power = true;
  M5.begin(cfg);
  
  // Initialize serial for debugging
  #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
    // Only enable Serial debug if NOT using Serial MIDI
    Serial.begin(SERIAL_BAUD_RATE);
    while (!Serial && millis() < 3000);
    Serial.println("=== LeslieLEDs Starting ===");
    Serial.printf("Platform: %s\n", PLATFORM_NAME);
    Serial.printf("LED Count: %d\n", LED_COUNT);
    Serial.printf("LED_DATA_PIN: GPIO %d\n", LED_DATA_PIN);
    Serial.printf("Target FPS: %d\n", LED_TARGET_FPS);
  #endif

  // Initialize display
  displayHandler.begin();
  
  // Initialize LED controller
  ledController.begin();
  
  // Initialize MIDI
  midiHandler.begin();
  
  // Connect components
  midiHandler.setLEDController(&ledController);
  midiHandler.setDisplayHandler(&displayHandler);
  displayHandler.setLEDController(&ledController);
  ledController.setDisplayHandler(&displayHandler);
  
  // Initialize button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
    Serial.println("Setup complete - Ready for MIDI");
  #endif
}

// ========================================
// Main Loop
// ========================================
void loop() {
  M5.update();
  
  // Handle button press for page switching
  if (M5.BtnA.wasPressed()) {
    displayHandler.handleButtonPress();
  }
  
  // Handle MIDI input
  midiHandler.update();
  
  // Update LED animations
  ledController.update();
  
  // Update display
  displayHandler.update();
  
  // Small yield to prevent watchdog issues
  yield();
}
