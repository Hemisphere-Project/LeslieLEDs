#include <Arduino.h>
#include <M5Unified.h>
#include <ESPNowDMX.h>
#include <ESPNowMeshClock.h>
#include <LedEngine.h>
#include "config.h"
#include "dmx_to_ledengine.h"

using namespace LedEngineLib;

// ========================================
// Global Objects
// ========================================
LedEngineConfig ledConfig;
LedEngine* ledEngine = nullptr;
DMXToLedEngine* dmxAdapter = nullptr;

ESPNowDMX espnowDMX;
ESPNowMeshClock meshClock;

bool dmxConnected = false;
unsigned long lastDMXFrame = 0;
const uint32_t DMX_TIMEOUT = 3000;

// Callback for DMX frame reception
void onDMXFrameReceived(uint8_t universe, const uint8_t* data) {
    (void)universe;
    if (dmxAdapter && data) {
        dmxAdapter->applyDMXFrame(data, DMX_UNIVERSE_SIZE);
        dmxConnected = true;
        lastDMXFrame = millis();
    }
}

// Quick RGBW test pattern so hardware faults are obvious during boot
void playBootRGBWTest() {
    if (!ledEngine) {
        return;
    }

    LedEngineState testState{};
    testState.masterBrightness = LED_BRIGHTNESS;
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
        ledEngine->show();
        delay(150);
    }

    // Return strip to black before waiting for DMX
    testState.colorA = ColorRGBW(0, 0, 0, 0);
    testState.colorB = testState.colorA;
    ledEngine->update(millis(), testState);
    ledEngine->show();
}

// ========================================
// Setup
// ========================================
void setup() {
    // Initialize M5 (buttons/power) regardless of display presence
    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.output_power = true;
    M5.begin(cfg);
    
    // Initialize serial for debugging
    #if DEBUG_MODE
        Serial.begin(SERIAL_BAUD_RATE);
        while (!Serial && millis() < 3000);
        Serial.println("=== DMXnow2Strip Starting ===");
        Serial.printf("Platform: %s\n", PLATFORM_NAME);
        Serial.printf("LED Count: %d\n", LED_COUNT);
        Serial.printf("LED_DATA_PIN: GPIO %d\n", LED_DATA_PIN);
    #endif

    // Initialize LED engine
    ledConfig.ledCount = LED_COUNT;
    ledConfig.dataPin = LED_DATA_PIN;
    ledConfig.targetFPS = LED_TARGET_FPS;
    ledConfig.defaultBrightness = LED_BRIGHTNESS;
    ledConfig.enableRGBW = true;
    
    ledEngine = new LedEngine(ledConfig);
    ledEngine->begin();
    playBootRGBWTest();
    
    // Initialize DMX adapter
    dmxAdapter = new DMXToLedEngine();
    
    // Initialize MeshClock (owns ESP-NOW driver) and forward non-clock packets to the DMX receiver
    meshClock.setUserCallback(ESPNowDMX::forwardPacket);
    meshClock.begin(true);

    // Initialize ESPNow DMX receiver (reuse MeshClock's ESP-NOW instance)
    espnowDMX.setUniverseId(DMX_UNIVERSE_ID);
    if (!espnowDMX.begin(ESPNOW_DMX_MODE_RECEIVER, false)) {
        #if DEBUG_MODE
            Serial.println("[ERR] Failed to initialize ESPNowDMX receiver");
        #endif
        while (true) {
            delay(1000);
        }
    }
    espnowDMX.setReceiveCallback(onDMXFrameReceived);
    
    #if DEBUG_MODE
        Serial.println("Setup complete");
        Serial.println("Waiting for DMX over ESP-NOW");
        Serial.println("MeshClock slave mode enabled");
    #endif
    
    // Display info on screen if available
    #if DISPLAY_ENABLED
        M5.Display.clear();
        M5.Display.setTextSize(2);
        M5.Display.setCursor(10, 10);
        M5.Display.println("DMXnow2Strip");
        M5.Display.setTextSize(1);
        M5.Display.setCursor(10, 40);
        M5.Display.printf("Platform: %s\n", PLATFORM_NAME);
        M5.Display.printf("LEDs: %d\n", LED_COUNT);
        M5.Display.printf("Pin: GPIO%d\n", LED_DATA_PIN);
        M5.Display.println("DMX: Waiting...");
        M5.Display.println("Clock: Slave");
    #endif
}

// ========================================
// Main Loop
// ========================================
void loop() {
    M5.update();
    
    // Update MeshClock (synchronize time)
    meshClock.loop();
    
    // Check DMX connection timeout
    if (dmxConnected && (millis() - lastDMXFrame > DMX_TIMEOUT)) {
        dmxConnected = false;
        #if DEBUG_MODE
            Serial.println("DMX connection lost");
        #endif
    }
    
    // Update LED animations
    if (ledEngine && dmxAdapter && dmxAdapter->hasState()) {
        ledEngine->update(meshClock.meshMillis(), dmxAdapter->getState());
        ledEngine->show();
    }
    
    #if DEBUG_MODE
        static unsigned long lastDebug = 0;
        if (millis() - lastDebug > 5000) {
            lastDebug = millis();
            const char* syncState = "Unknown";
            switch (meshClock.getSyncState()) {
                case SyncState::ALONE: syncState = "Alone"; break;
                case SyncState::SYNCED: syncState = "Synced"; break;
                case SyncState::LOST: syncState = "Lost"; break;
            }
            Serial.printf("DMX: %s, Clock: %lu ms, Sync: %s, FPS: %d\n",
                         dmxConnected ? "Connected" : "Waiting",
                         meshClock.meshMillis(),
                         syncState,
                         ledEngine ? ledEngine->getFPS() : 0);
        }
    #endif
    
    yield();
}
