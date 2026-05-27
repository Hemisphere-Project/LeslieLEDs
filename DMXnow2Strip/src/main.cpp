#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <ESPNowDMX.h>
#include <ESPNowMeshClock.h>
#include <LedEngine.h>
#include "config.h"
#include "dmx_to_ledengine.h"
#include "boot_beacon.h"

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

// Self-heal: if no DMX frame has been seen for this long AND MeshClock
// reports LOST, restart the chip. Pure recovery — if the Wi-Fi stack
// quietly dies, the box restarts itself rather than going dark until
// someone power-cycles it. Cold-boot freezes are NOT addressed by this
// (those need a physical RESET / hardware POR fix).
const uint32_t RADIO_SILENCE_RESTART_MS = 10000;
unsigned long lastRadioActivity = 0;

// Task WDT timeout. Generous enough to cover the RGBW boot sweep
// (~600 ms) plus the ESPNowDMX init retries (up to ~600 ms) plus a
// healthy margin. If setup() ever hangs past this, the chip self-resets
// instead of needing a manual RESET press.
const uint32_t WDT_TIMEOUT_MS = 15000;

BootBeacon beacon;

// Reset-reason rolling log in NVS. No serial required.
// Read back with `pio device monitor` after pressing the screen-less
// node's reset button, or expose later via SysEx.
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
    // Arduino-ESP32 may have already initialised the WDT; reconfigure
    // covers both cases without erroring.
    if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) {
        esp_task_wdt_init(&cfg);
    }
#else
    esp_task_wdt_init(WDT_TIMEOUT_MS / 1000, true);
#endif
    // ESP_ERR_INVALID_ARG just means the current task is already subscribed.
    esp_task_wdt_add(nullptr);
    esp_task_wdt_reset();
}

// Mailbox between the Wi-Fi RX context and the main loop. The ESPNowDMX
// receive callback runs inside the Wi-Fi task and must not do heavy work
// (heatshrink decompression, full DMX channel decode, HSV->RGBW, etc.):
// long callbacks back-pressure the Wi-Fi stack and have been observed to
// freeze slave nodes under sustained traffic. We capture the latest frame
// into a single-slot mailbox here and let the main loop apply it.
static volatile bool g_pendingFrame = false;
static volatile uint32_t g_pendingFrameRxMillis = 0;
static uint8_t g_pendingFrameBuf[DMX_UNIVERSE_SIZE];
static portMUX_TYPE g_pendingFrameMux = portMUX_INITIALIZER_UNLOCKED;

// Callback for DMX frame reception - runs in Wi-Fi task context, keep short.
void onDMXFrameReceived(uint8_t universe, const uint8_t* data) {
    (void)universe;
    if (!data) return;
    portENTER_CRITICAL(&g_pendingFrameMux);
    memcpy(g_pendingFrameBuf, data, DMX_UNIVERSE_SIZE);
    g_pendingFrameRxMillis = millis();
    g_pendingFrame = true;
    portEXIT_CRITICAL(&g_pendingFrameMux);
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
#if !defined(ARDUINO_ARCH_ESP32)
    ledEngine->show();
#endif
        delay(150);
    }

    // Return strip to black before waiting for DMX
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
    // Record why we last restarted (PowerOn / Brownout / TaskWDT / Panic / ...)
    // BEFORE anything that could itself hang or panic. If the bad cold-boot
    // scenario ever puts us in here under a non-POWERON reason, we'll know.
    bootlog::record(esp_reset_reason());

    // Boot watchdog: gives the chip a self-reset path if any of the init
    // steps below ever hangs (Wi-Fi bringup, RMT alloc, etc.). Does not
    // help with pre-app POR freezes, but turns every other init-stall
    // failure into an automatic reboot instead of requiring a person.
    wdtSetup();

    // Onboard LED beacon: RED solid the moment setup() starts so you can
    // see across the room which units made it past power-up.
    beacon.begin(LED_BUILTIN);
    beacon.setState(BootBeacon::BOOT);

    // Boot delay - let power rails and WiFi radio stabilize after cold boot
    delay(500);
    esp_task_wdt_reset();

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
    esp_task_wdt_reset();

    // Initialize DMX adapter
    dmxAdapter = new DMXToLedEngine();
    
    // Initialize MeshClock (owns ESP-NOW driver) and forward non-clock packets to the DMX receiver
    meshClock.setUserCallback(ESPNowDMX::forwardPacket);
    meshClock.begin(true);

    // Initialize ESPNow DMX receiver with retry logic
    espnowDMX.setUniverseId(DMX_UNIVERSE_ID);
    
    const int maxRetries = 3;
    bool espnowSuccess = false;
    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        #if DEBUG_MODE
            Serial.printf("[INFO] ESPNowDMX init attempt %d/%d\n", attempt, maxRetries);
        #endif

        if (espnowDMX.begin(ESPNOW_DMX_MODE_RECEIVER, false)) {
            espnowSuccess = true;
            #if DEBUG_MODE
                Serial.println("[OK] ESPNowDMX receiver initialized");
            #endif
            break;
        }

        #if DEBUG_MODE
            Serial.println("[WARN] ESPNowDMX init failed, retrying...");
        #endif
        esp_task_wdt_reset();
        delay(200);  // Wait before retry
    }
    
    if (!espnowSuccess) {
        #if DEBUG_MODE
            Serial.println("[ERR] ESPNowDMX receiver failed after retries, restarting...");
        #endif
        delay(1000);
        ESP.restart();
    }
    
    espnowDMX.setReceiveCallback(onDMXFrameReceived);

    // Setup complete — flip the beacon to green.
    beacon.setState(BootBeacon::READY);
    lastRadioActivity = millis();

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
    esp_task_wdt_reset();

    M5.update();
    uint32_t now = millis();

    // Update MeshClock (synchronize time)
    meshClock.loop();

    // Track any sign of life from the radio: either a DMX frame or a clock
    // packet. Used by the radio-silence self-heal below.
    if (g_pendingFrame || meshClock.getSyncState() == SyncState::SYNCED) {
        lastRadioActivity = now;
    }

    // Drain the mailbox: at most one DMX frame per loop iteration.
    // Doing the decode here (instead of inside the Wi-Fi RX callback) keeps
    // the radio stack responsive and removes the cross-thread race between
    // applyDMXFrame() writing _state and ledEngine->update() reading it.
    if (g_pendingFrame) {
        static uint8_t frameCopy[DMX_UNIVERSE_SIZE];
        uint32_t rxMillis;
        portENTER_CRITICAL(&g_pendingFrameMux);
        memcpy(frameCopy, g_pendingFrameBuf, DMX_UNIVERSE_SIZE);
        rxMillis = g_pendingFrameRxMillis;
        g_pendingFrame = false;
        portEXIT_CRITICAL(&g_pendingFrameMux);

        if (dmxAdapter) {
            bool wasDisconnected = !dmxConnected;
            dmxAdapter->applyDMXFrame(frameCopy, DMX_UNIVERSE_SIZE);
            dmxConnected = true;
            lastDMXFrame = rxMillis;
            if (wasDisconnected) {
                beacon.setState(BootBeacon::READY);
            }
        }
    }

    // Check DMX connection timeout
    if (dmxConnected && (now - lastDMXFrame > DMX_TIMEOUT)) {
        dmxConnected = false;
        beacon.setState(BootBeacon::LOST);
        #if DEBUG_MODE
            Serial.println("DMX connection lost");
        #endif
    }
    beacon.tick(now);

    // Radio-silence self-heal: if we've heard nothing on the radio for a
    // sustained period, the Wi-Fi stack has likely wedged. Reboot.
    if (now - lastRadioActivity > RADIO_SILENCE_RESTART_MS) {
        #if DEBUG_MODE
            Serial.println("[RADIO] No activity for >10s — restarting");
            Serial.flush();
        #endif
        delay(50);
        ESP.restart();
    }

    // Update LED animations
    if (ledEngine && dmxAdapter && dmxAdapter->hasState()) {
    ledEngine->update(meshClock.meshMillis(), dmxAdapter->getState());
#if !defined(ARDUINO_ARCH_ESP32)
    ledEngine->show();
#endif
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
