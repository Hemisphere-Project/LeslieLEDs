#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"
#include "midi_handler.h"
#include "led_controller.h"
#include "display_handler.h"

// ========================================
// Global Objects
// ========================================
MIDIHandler midiHandler;
LEDController ledController;
DisplayHandler displayHandler;

// ========================================
// Setup
// ========================================
void setup() {
  // Initialize M5AtomS3
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power = true;
  M5.begin(cfg);
  
  // Initialize serial for debugging
  #if DEBUG_MODE
    Serial.begin(SERIAL_BAUD_RATE);
    while (!Serial && millis() < 3000);
    Serial.println("=== LeslieLEDs Starting ===");
    Serial.printf("LED Count: %d\n", LED_COUNT);
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
  
  // Initialize button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  #if DEBUG_MODE
    Serial.println("Setup complete - Ready for MIDI");
  #endif
}

// ========================================
// Main Loop
// ========================================
void loop() {
  M5.update();
  
  // Handle MIDI input
  midiHandler.update();
  
  // Update LED animations
  ledController.update();
  
  // Update display
  displayHandler.update();
  
  // Small yield to prevent watchdog issues
  yield();
}
