#ifndef DISPLAY_HANDLER_H
#define DISPLAY_HANDLER_H

#include <Arduino.h>
#include <M5Unified.h>
#include <LedEngine.h>
#include "config.h"
#include "dmx_state.h"
#include "led_preview_renderer.h"
#include "heartbeat_collector.h"

class DisplayHandler {
public:
    DisplayHandler();

    void begin();
    void update();

    void setLedEngine(LedEngineLib::LedEngine* engine);
    void setDMXState(DMXState* state);
    void setHeartbeats(const HeartbeatCollector* hb) { _heartbeats = hb; }
    void logMessage(const char* message);
    void showSceneNotification(uint8_t sceneNumber, bool isSave);

    // Button press now loads next scene
    void handleButtonPress();

    // Track current scene (called when scene changes via MIDI or button)
    void setCurrentScene(int8_t scene) { _currentScene = scene; }

private:
    LedEngineLib::LedEngine* _ledEngine;
    DMXState* _dmxState;
    const HeartbeatCollector* _heartbeats;

    unsigned long _lastUpdate;
    unsigned long _sceneNotificationEnd;
    uint8_t _sceneNotificationNumber;
    bool _sceneNotificationIsSave;
    bool _needsFullRedraw;
    int8_t _currentScene;  // Current scene (0-9), -1 if none
    unsigned long _lastPreviewUpdate;

    LedPreviewRenderer _previewRenderer;

    void drawPreview();
    void drawSceneIndicator();
    void drawSceneNotification();
    void drawHeartbeatDots();
};

#endif // DISPLAY_HANDLER_H
