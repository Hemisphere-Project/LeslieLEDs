#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_now.h>
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

// Self-heal timers:
//   RADIO_SILENCE_RESTART_MS – no clock OR DMX from any radio source for
//     this long → the Wi-Fi stack itself is probably wedged. Restart.
//   NO_DMX_RESTART_MS – MeshClock is synced (clock packets are arriving)
//     but no DMX frame has been applied for this long → the ESPNowDMX
//     receive path is wedged while the radio is otherwise fine. Restart.
// Both timers are needed because the radio-silence check is gated on
// *any* radio activity (including clock packets), so it never fires when
// only the DMX receive path is stuck.
const uint32_t RADIO_SILENCE_RESTART_MS = 10000;
const uint32_t NO_DMX_RESTART_MS        = 30000;
const uint32_t FRESH_CLOCK_ACTIVITY_MS  = 2000;
unsigned long lastRadioActivity = 0;
unsigned long noDMXWatchdogSince = 0;

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

// --- Heartbeat emitter ------------------------------------------------
// Sends a HeartbeatPacket to the broadcast address once per
// LESLIE_HEARTBEAT_PERIOD_MS. Sender (Midi2DMXnow) collects them so the
// operator sees per-node health on the master screen without USB.
// Other slaves see the packet too but their ESPNowDMX receiver drops
// anything that isn't PACKET_TYPE_DATA_CHUNK, so it's free.
static const uint8_t kBroadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t g_nid[3] = {0, 0, 0};
static uint32_t g_lastHeartbeatSent = 0;
static esp_reset_reason_t g_lastResetReason = ESP_RST_UNKNOWN;

static void initNodeId() {
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        g_nid[0] = mac[3];
        g_nid[1] = mac[4];
        g_nid[2] = mac[5];
    }
}

static void sendHeartbeatIfDue(uint32_t now) {
    if (now - g_lastHeartbeatSent < LESLIE_HEARTBEAT_PERIOD_MS) return;
    g_lastHeartbeatSent = now;

    HeartbeatPacket pkt{};
    pkt.type = PACKET_TYPE_HEARTBEAT;
    pkt.version = LESLIE_HEARTBEAT_VERSION;
    pkt.nid[0] = g_nid[0];
    pkt.nid[1] = g_nid[1];
    pkt.nid[2] = g_nid[2];
    pkt.lastResetReason = (uint8_t)g_lastResetReason;
    pkt.uptimeSec = now / 1000;
    pkt.freeHeap = ESP.getFreeHeap();
    pkt.msSinceLastFrame = (lastDMXFrame == 0)
        ? UINT32_MAX
        : (uint32_t)(now - lastDMXFrame);
    // Skip cached FPS lookup if ledEngine isn't up yet (shouldn't happen
    // post-setup, but be defensive).
    extern LedEngine* ledEngine;
    pkt.fps = ledEngine ? ledEngine->getFPS() : 0;

    // esp_now_send return value is ignored on purpose: we don't want
    // heartbeat send failures to bump the ESPNowDMX_Sender error
    // counters (slaves don't have the sender wired). A dropped HB
    // just means one missed sample on the operator's grid.
    (void)esp_now_send(kBroadcastAddr, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}
// --- end heartbeat emitter -------------------------------------------

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
        // Spin-wait instead of delay() so the WDT stays fed and FreeRTOS
        // tasks (render task on core 1) can run between color steps.
        uint32_t stepStart = millis();
        while (millis() - stepStart < 150) {
            esp_task_wdt_reset();
            vTaskDelay(1);
        }
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
    esp_reset_reason_t lastReason = esp_reset_reason();
    g_lastResetReason = lastReason;
    bootlog::record(lastReason);

    // Boot watchdog: gives the chip a self-reset path if any of the init
    // steps below ever hangs (Wi-Fi bringup, RMT alloc, etc.). Does not
    // help with pre-app POR freezes, but turns every other init-stall
    // failure into an automatic reboot instead of requiring a person.
    wdtSetup();

    // Onboard LED beacon: RED solid the moment setup() starts so you can
    // see across the room which units made it past power-up. If we just
    // came back from a brownout, flash purple instead to flag it.
    beacon.begin(LED_BUILTIN);
    if (lastReason == ESP_RST_BROWNOUT) {
        beacon.setState(BootBeacon::BROWNOUT);
    } else {
        beacon.setState(BootBeacon::BOOT);
    }

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
        Serial.printf("Last reset reason: %d\n", (int)lastReason);
    #endif

    // Initialize LED engine
    ledConfig.ledCount = LED_COUNT;
    ledConfig.dataPin = LED_DATA_PIN;
    ledConfig.targetFPS = LED_TARGET_FPS;
    ledConfig.defaultBrightness = LED_BRIGHTNESS;
    ledConfig.enableRGBW = true;

    ledEngine = new LedEngine(ledConfig);
    ledEngine->begin();
    // Skip the bright RGBW sweep if we just browned out: lighting 120
    // SK6812s at full white draws ~7 A, which is exactly the kind of
    // spike that would re-trigger the brownout in a loop. The purple
    // beacon flag from above already tells the operator what happened.
    if (lastReason != ESP_RST_BROWNOUT) {
        playBootRGBWTest();
    }
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

    // Pull the chip's WiFi MAC so heartbeats carry a stable per-node
    // identifier. esp_read_mac is safe to call now that WiFi.mode(STA)
    // has been set inside ESPNowMeshClock::begin().
    initNodeId();

    // Setup complete — flip the beacon to green UNLESS we're still
    // flagging a brownout from the previous reset. The purple pattern
    // persists until the first DMX frame arrives, at which point the
    // mailbox-drain path in loop() will flip it to READY.
    if (lastReason != ESP_RST_BROWNOUT) {
        beacon.setState(BootBeacon::READY);
    }
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
    SyncState syncState = meshClock.getSyncState();

    // Update MeshClock (synchronize time)
    meshClock.loop();

    syncState = meshClock.getSyncState();
    uint32_t msSinceClockSync = meshClock.msSinceLastSync();
    bool freshClockActivity =
        syncState == SyncState::SYNCED &&
        msSinceClockSync <= FRESH_CLOCK_ACTIVITY_MS;

    // Once we've synced to the mesh at least once, start a watchdog for
    // the DMX receive path even if the first DMX frame never arrives.
    if (freshClockActivity) {
        if (noDMXWatchdogSince == 0) {
            noDMXWatchdogSince = now;
        }
    } else {
        noDMXWatchdogSince = 0;
    }

    // Track any sign of life from the radio: either a DMX frame or a clock
    // packet. Used by the radio-silence self-heal below.
    if (g_pendingFrame || freshClockActivity) {
        lastRadioActivity = now;
    }

    // Drain the mailbox: at most one DMX frame per loop iteration.
    // Doing the decode here (instead of inside the Wi-Fi RX callback) keeps
    // the radio stack responsive and removes the cross-thread race between
    // applyDMXFrame() writing _state and ledEngine->update() reading it.
    if (g_pendingFrame) {
        static uint8_t frameCopy[DMX_UNIVERSE_SIZE];
        portENTER_CRITICAL(&g_pendingFrameMux);
        memcpy(frameCopy, g_pendingFrameBuf, DMX_UNIVERSE_SIZE);
        g_pendingFrame = false;
        portEXIT_CRITICAL(&g_pendingFrameMux);

        if (dmxAdapter) {
            bool wasDisconnected = !dmxConnected;
            dmxAdapter->applyDMXFrame(frameCopy, DMX_UNIVERSE_SIZE);
            dmxConnected = true;
            // Use 'now' (sampled at loop top) rather than the ISR's
            // g_pendingFrameRxMillis. The RX callback fires in the Wi-Fi
            // task and its millis() stamp can be fractionally *ahead* of
            // 'now', causing now-lastDMXFrame to underflow to UINT32_MAX
            // and immediately trigger both the 3 s DMX timeout and the
            // 30 s no-DMX watchdog.
            lastDMXFrame = now;
            noDMXWatchdogSince = now;
            if (wasDisconnected) {
                beacon.setState(BootBeacon::READY);
            }
            beacon.noteDMXActivity(now);
        }
    }

    // Emit per-second heartbeat to the master (and ignored by other slaves).
    sendHeartbeatIfDue(now);

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

    // DMX-receive-path self-heal: if fresh clock packets are still arriving
    // (so the radio is alive) but no DMX frame has been
    // applied for 30 s, the ESPNowDMX receiver is wedged. Reboot.
    if (noDMXWatchdogSince > 0 &&
        freshClockActivity &&
        (now - noDMXWatchdogSince) > NO_DMX_RESTART_MS) {
        #if DEBUG_MODE
            Serial.printf("[DMX] No DMX for %lu ms while clock age is %lu ms — restarting\n",
                          (unsigned long)(now - noDMXWatchdogSince),
                          (unsigned long)msSinceClockSync);
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
            const char* syncStateLabel = "Unknown";
            switch (syncState) {
                case SyncState::ALONE: syncStateLabel = "Alone"; break;
                case SyncState::SYNCED: syncStateLabel = "Synced"; break;
                case SyncState::LOST: syncStateLabel = "Lost"; break;
            }
            Serial.printf("DMX: %s, Clock: %lu ms, Sync: %s, FPS: %d\n",
                         dmxConnected ? "Connected" : "Waiting",
                         meshClock.meshMillis(),
                         syncStateLabel,
                         ledEngine ? ledEngine->getFPS() : 0);
        }
    #endif
    
    yield();
}
