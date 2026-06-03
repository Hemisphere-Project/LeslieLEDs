#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <ESPNowDMX.h>
#include <ESPNowMeshClock.h>
#include <LedEngine.h>
#include "config.h"
#include "dmx_state.h"
#include "dmx_output.h"
#include "display_handler.h"
#include "heartbeat_collector.h"

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
PhysicalDMXOutput physicalDMX;
HeartbeatCollector heartbeats;

// Dispatcher used as MeshClock's userCallback (replaces the older
// ESPNowDMX::forwardPacket on the sender side). The sender has no
// active ESPNowDMX receiver — it's the source of DMX — so we route by
// packet type: heartbeat packets feed the rig-health collector,
// everything else is dropped silently.
static void onRadioPacket(const uint8_t* mac, const uint8_t* data, int len) {
    (void)mac;
    if (len < 1) return;
    if (data[0] == PACKET_TYPE_HEARTBEAT) {
        heartbeats.ingest(data, len, millis());
    }
    // PACKET_TYPE_DATA_CHUNK from another sender, unknown types: drop.
}

// LED monitoring strip
LedEngineConfig ledConfig;
LedEngine* ledEngine = nullptr;

uint8_t dmxFrame[DMX_UNIVERSE_SIZE];
unsigned long lastDMXSend = 0;
const uint32_t DMX_SEND_INTERVAL = 33; // ~30Hz DMX refresh rate

// Task WDT: if setup() or loop() ever hangs past this, the chip self-resets.
const uint32_t WDT_TIMEOUT_MS = 15000;

// Rig-health watchdog: if we have ≥1 active slave but every one of them
// has been reporting "no DMX" for this long, our ESP-NOW send path is
// silently wedged (esp_now_send returns OK but packets don't arrive).
// Restarting resets the radio stack, which fixes the issue.
const uint32_t RIG_NO_DMX_RESTART_MS = 60000;  // 1 minute

// Reset-reason rolling log in NVS. No serial required to read it back later.
namespace bootlog {
    constexpr const char* NAMESPACE = "bootlog";
    constexpr const char* KEY_REASONS = "reasons";
    constexpr const char* KEY_COUNT = "count";
    constexpr size_t MAX_ENTRIES = 16;

    void record(esp_reset_reason_t reason) {
        Preferences p;
        if (!p.begin(NAMESPACE, false)) return;

        uint8_t buf[MAX_ENTRIES];
        size_t len = p.getBytesLength(KEY_REASONS);
        if (len > MAX_ENTRIES) len = MAX_ENTRIES;
        if (len > 0) p.getBytes(KEY_REASONS, buf, len);

        if (len >= MAX_ENTRIES) {
            memmove(buf, buf + 1, MAX_ENTRIES - 1);
            len = MAX_ENTRIES - 1;
        }
        buf[len++] = static_cast<uint8_t>(reason);

        p.putBytes(KEY_REASONS, buf, len);
        p.putUInt(KEY_COUNT, p.getUInt(KEY_COUNT, 0) + 1);
        p.end();
    }
}

static void wdtSetup() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t cfg = {
        .timeout_ms = WDT_TIMEOUT_MS,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) {
        esp_task_wdt_init(&cfg);
    }
#else
    esp_task_wdt_init(WDT_TIMEOUT_MS / 1000, true);
#endif
    esp_task_wdt_add(nullptr);
    esp_task_wdt_reset();
}

// Quick RGBW sweep lets us spot wiring faults before DMX starts
void playBootRGBWTest() {
  if (!ledEngine) {
    return;
  }

  LedEngineState testState{};
  testState.masterBrightness = ledConfig.defaultBrightness;
  testState.mode = AnimationMode::ANIM_SOLID;
  testState.animationSpeed = 0;
  testState.animationCtrl = 0;
  testState.strobeRate = 0;
  testState.blendMode = 0;
  testState.mirror = MirrorMode::MIRROR_NONE;
  testState.direction = DirectionMode::DIR_FORWARD;

  const ColorRGBW testColors[4] = {
    ColorRGBW(255, 0, 0, 0),
    ColorRGBW(0, 255, 0, 0),
    ColorRGBW(0, 0, 255, 0),
    ColorRGBW(0, 0, 0, 255)
  };

  for (uint8_t i = 0; i < 4; ++i) {
    testState.colorA = testColors[i];
    testState.colorB = testColors[i];
    ledEngine->update(millis(), testState);
#if !defined(ARDUINO_ARCH_ESP32)
    ledEngine->show();
#endif
    uint32_t stepStart = millis();
    while (millis() - stepStart < 150) {
      esp_task_wdt_reset();
      vTaskDelay(1);
    }
  }

  // Return to black before regular rendering resumes
  testState.colorA = ColorRGBW(0, 0, 0, 0);
  testState.colorB = testState.colorA;
  ledEngine->update(millis(), testState);
#if !defined(ARDUINO_ARCH_ESP32)
  ledEngine->show();
#endif
}

// ========================================
// Setup
// ========================================
void setup() {
  bootlog::record(esp_reset_reason());
  wdtSetup();

  // Initialize M5
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power = true;
  M5.begin(cfg);
  displayHandler.begin();
  esp_task_wdt_reset();
  
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
  ledConfig.defaultBrightness = 10;
  ledConfig.enableRGBW = true;
  
  ledEngine = new LedEngine(ledConfig);
  ledEngine->begin();
  playBootRGBWTest();
  esp_task_wdt_reset();
  displayHandler.setLedEngine(ledEngine);
  displayHandler.setDMXState(&dmxState);
  displayHandler.setHeartbeats(&heartbeats);
  
  // Initialize MIDI
  midiHandler.begin();
  
  midiHandler.setDMXState(&dmxState);
  midiHandler.setDisplayHandler(&displayHandler);
  midiHandler.setHeartbeats(&heartbeats);
  
  // Initialize MeshClock so it owns the ESP-NOW driver and forwards
  // non-clock packets to our dispatcher (heartbeat collector + DMX drop).
  meshClock.setUserCallback(onRadioPacket);
  meshClock.begin(true);

  // Initialize ESPNow DMX sender (reuse MeshClock's ESP-NOW stack)
  espnowDMX.setUniverseId(DMX_UNIVERSE_ID);
  if (!espnowDMX.begin(ESPNOW_DMX_MODE_SENDER, false)) {
    #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
      Serial.println("[ERR] Failed to initialize ESPNowDMX sender");
    #endif
    while (true) {
      delay(1000);
    }
  }
  
  // Initialize Physical DMX output on PortC
  PhysicalDMXOutput::Config dmxOutputConfig;
  dmxOutputConfig.txPin = DMX_OUTPUT_TX_PIN;
  dmxOutputConfig.rxPin = DMX_OUTPUT_RX_PIN;
  dmxOutputConfig.enablePin = DMX_OUTPUT_ENABLE_PIN;
  dmxOutputConfig.dmxPort = DMX_OUTPUT_PORT;
  dmxOutputConfig.refreshIntervalMs = DMX_OUTPUT_REFRESH_MS;
  
  if (!physicalDMX.begin(dmxOutputConfig)) {
    #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
      Serial.println("[WARN] Physical DMX output failed to initialize");
    #endif
  }
  
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
  esp_task_wdt_reset();

  M5.update();

  if (M5.BtnA.wasPressed()) {
    displayHandler.handleButtonPress();
  }

  // Update MeshClock timing
  meshClock.loop();

  const uint32_t now = millis();

  // Age out stale heartbeats so the dot row reflects current state.
  heartbeats.prune(now);

  // Rig-health watchdog: detect silent ESP-NOW send-path failure.
  // If ≥1 slave is actively sending heartbeats but NONE of them have
  // received a DMX frame recently, the sender is wedged. Self-heal.
  {
    static uint32_t rigNoDmxSince = 0;
    HeartbeatCollector::Slot hbSlots[HeartbeatCollector::MAX_SLAVES];
    heartbeats.copySlots(hbSlots);
    bool hasActiveSlave = false;
    bool anySlaveHasDmx  = false;
    for (uint8_t i = 0; i < HeartbeatCollector::MAX_SLAVES; ++i) {
      if (!hbSlots[i].used) continue;
      if ((now - hbSlots[i].lastHeardLocalMs) >= HeartbeatCollector::STALE_MS) continue;
      hasActiveSlave = true;
      if (hbSlots[i].last.msSinceLastFrame < HeartbeatCollector::NO_DMX_FRAME_MS) {
        anySlaveHasDmx = true;
      }
    }
    if (hasActiveSlave && !anySlaveHasDmx) {
      if (rigNoDmxSince == 0) rigNoDmxSince = now;
      else if (now - rigNoDmxSince > RIG_NO_DMX_RESTART_MS) {
        #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
          Serial.printf("[RIG] All %u active slave(s) report no DMX for >%lus — restarting\n",
                        heartbeats.activeCount(now), RIG_NO_DMX_RESTART_MS / 1000);
          Serial.flush();
        #endif
        displayHandler.logMessage("RIG watchdog: restarting");
        delay(50);
        ESP.restart();
      }
    } else {
      rigNoDmxSince = 0;
    }
  }

  // Handle MIDI input
  midiHandler.update();
  
  // Update LED monitor to visualize current state
  if (ledEngine) {
    LedEngineState state = dmxState.toLedEngineState();
    ledEngine->update(meshClock.meshMillis(), state);
#if !defined(ARDUINO_ARCH_ESP32)
    ledEngine->show();
#endif
  }

  ESPNowDMX_Sender::SendStats stats = ESPNowDMX_Sender::getSendStats();
  displayHandler.setRadioStatus(stats.totalFailed, stats.consecutiveFailures);
  displayHandler.update();
  
  // Generate and send DMX frame at regular intervals
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
  
  // Update physical DMX output (RGBW on channels 1-4)
  // Uses its own rate limiting internally
  physicalDMX.update(dmxFrame, meshClock.meshMillis());

  // Sender self-heal: if every ESP-NOW broadcast for the last few
  // seconds has failed, the radio stack is wedged. Restart symmetric
  // with what slaves do on prolonged radio silence. Threshold is
  // 100 consecutive failures (~3 s at 30 Hz) — well above transient
  // collisions during heavy CC bursts.
  if (stats.consecutiveFailures > 100) {
    #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
      Serial.printf("[RADIO] %u consecutive send failures — restarting\n",
                    stats.consecutiveFailures);
      Serial.flush();
    #endif
    delay(50);
    ESP.restart();
  }
  
  yield();
}
