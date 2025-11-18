#include <Arduino.h>
#include <M5Unified.h>
#include <ESPNowDMX.h>
#include <ESPNowMeshClock.h>
#include <LedEngine.h>
#include "config.h"
#include "dmx_state.h"
#include "display_handler.h"

// Platform-specific MIDI handler
#if MIDI_VIA_SERIAL
  #include "serial_midi_handler.h"
  SerialMIDIHandler midiHandler;
#else
  #include "midi_handler.h"
  MIDIHandler midiHandler;
#endif

using namespace LedEngineLib;

// ========================================
// Global Objects
// ========================================
DMXState dmxState;
ESPNowDMX espnowDMX;
ESPNowMeshClock meshClock;
DisplayHandler displayHandler;

// LED monitoring strip
LedEngineConfig ledConfig;
LedEngine* ledEngine = nullptr;

uint8_t dmxFrame[DMX_UNIVERSE_SIZE];
unsigned long lastDMXSend = 0;
const uint32_t DMX_SEND_INTERVAL = 33; // ~30Hz DMX refresh rate

// ========================================
// Setup
// ========================================
void setup() {
  // Initialize M5
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power = true;
  M5.begin(cfg);
  displayHandler.begin();
  
  // Initialize serial for debugging
  #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
    Serial.begin(SERIAL_BAUD_RATE);
    while (!Serial && millis() < 3000);
    Serial.println("=== Midi2DMXnow Starting ===");
    Serial.printf("Platform: %s\n", PLATFORM_NAME);
    Serial.printf("MIDI Mode: %s\n", COMM_MODE);
  #endif

  // Initialize DMX state
  dmxState.begin();
  
  // Initialize LED monitoring strip
  ledConfig.ledCount = LED_COUNT;
  ledConfig.dataPin = LED_DATA_PIN;
  ledConfig.targetFPS = 60;
  ledConfig.defaultBrightness = 128;
  ledConfig.enableRGBW = true;
  
  ledEngine = new LedEngine(ledConfig);
  ledEngine->begin();
  displayHandler.setLedEngine(ledEngine);
  displayHandler.setDMXState(&dmxState);
  
  // Initialize MIDI
  midiHandler.begin();
  
  midiHandler.setDMXState(&dmxState);
  midiHandler.setDisplayHandler(&displayHandler);
  
  // Initialize ESPNow DMX sender
  espnowDMX.setUniverseId(DMX_UNIVERSE_ID);
  if (!espnowDMX.begin(ESPNOW_DMX_MODE_SENDER)) {
    #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
      Serial.println("[ERR] Failed to initialize ESPNowDMX sender");
    #endif
    while (true) {
      delay(1000);
    }
  }
  
  // Initialize MeshClock and register ESP-NOW callback chain
  meshClock.begin(true);
  
  #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
    Serial.println("Setup complete - Ready for MIDI");
    Serial.println("Broadcasting DMX over ESP-NOW");
    Serial.println("MeshClock master mode enabled");
    Serial.printf("LED Monitor: %d LEDs on GPIO%d\n", LED_COUNT, LED_DATA_PIN);
  #endif
  
}

// ========================================
// Main Loop
// ========================================
void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    displayHandler.handleButtonPress();
  }
  
  // Update MeshClock timing
  meshClock.loop();
  
  // Handle MIDI input
  midiHandler.update();
  
  // Update LED monitor to visualize current state
  if (ledEngine) {
    LedEngineState state = dmxState.toLedEngineState();
    ledEngine->update(meshClock.meshMillis(), state);
    ledEngine->show();
  }

  displayHandler.update();
  
  // Generate and send DMX frame at regular intervals
  unsigned long now = millis();
  if (now - lastDMXSend >= DMX_SEND_INTERVAL) {
    lastDMXSend = now;
    
    // Generate DMX frame from current state
    dmxState.toDMXFrame(dmxFrame, DMX_UNIVERSE_SIZE);
    
    // Broadcast DMX via ESP-NOW
    espnowDMX.sendDMXFrame(dmxFrame, DMX_UNIVERSE_SIZE);
    
    #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
      static int frameCount = 0;
      if (++frameCount % 100 == 0) {
        Serial.printf("Sent %d DMX frames, Clock: %lu ms\n", 
                frameCount, meshClock.meshMillis());
      }
    #endif
  }
  
  yield();
}
