#ifndef DISPLAY_HANDLER_H
#define DISPLAY_HANDLER_H

#include <Arduino.h>
#include <M5Unified.h>
#include <LedEngine.h>
#include "config.h"
#include "dmx_state.h"

class DisplayHandler {
public:
    DisplayHandler();

    void begin();
    void update();

    void setLedEngine(LedEngineLib::LedEngine* engine);
    void setDMXState(DMXState* state);
    void logMessage(const char* message);
    void showSceneNotification(uint8_t sceneNumber, bool isSave);
    void handleButtonPress();

private:
    LedEngineLib::LedEngine* _ledEngine;
    DMXState* _dmxState;

    struct LogEntry {
        char text[32];
        unsigned long timestamp;
    };

    LogEntry _logEntries[MIDI_LOG_LINES];
    int _logIndex;

    unsigned long _lastUpdate;
    unsigned long _sceneNotificationEnd;
    uint8_t _sceneNotificationNumber;
    bool _sceneNotificationIsSave;
    bool _needsFullRedraw;
    uint8_t _currentPage;
    unsigned long _lastPreviewUpdate;

    struct DisplayState {
        LedEngineLib::AnimationMode mode;
        uint8_t brightness;
        uint8_t speed;
        LedEngineLib::ColorRGBW colorA;
        LedEngineLib::ColorRGBW colorB;
        uint8_t fps;
        int lastLogIndex;
    } _lastState;

    static constexpr uint8_t PAGE_COUNT = 3;
    static constexpr uint8_t PREVIEW_PIXEL_SIZE = 4;
    static constexpr uint8_t PREVIEW_ROW_SPACING = 4;

    void drawUI();
    void drawPagePreview();
    void drawPageParameters();
    void drawPageLogs();
    void drawStatusBar();
    void drawInfoPanel();
    void drawMessageLog();
    void drawSceneNotification();
    bool hasStateChanged();
    LedEngineLib::LedEngineState currentEngineState() const;
};

#endif // DISPLAY_HANDLER_H
