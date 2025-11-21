#ifndef DISPLAY_HANDLER_H
#define DISPLAY_HANDLER_H

#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"

// Forward declarations
class LEDController;
struct ColorRGBW;

class DisplayHandler {
public:
    DisplayHandler();
    
    void begin();
    void update();
    
    void setLEDController(LEDController* controller);
    void logMessage(const char* message);
    void showSceneNotification(uint8_t sceneNumber, bool isSave);
    void handleButtonPress();
    
private:
    LEDController* _ledController;
    
    // Message log
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
    
    // State tracking for selective updates
    struct DisplayState {
        AnimationMode mode;
        uint8_t brightness;
        uint32_t fps;
        uint8_t colorA_r, colorA_g, colorA_b, colorA_w;
        uint8_t colorB_r, colorB_g, colorB_b, colorB_w;
        uint8_t speed;
        int lastLogIndex;
    };
    DisplayState _lastState;
    bool _needsFullRedraw;
    
    // Page management
    uint8_t _currentPage;
    unsigned long _lastPreviewUpdate;
    static const uint8_t PAGE_COUNT = 3;
    static const uint8_t PREVIEW_PIXEL_SIZE = 4;  // LCD pixels per LED (1, 2, or 4)
    static const uint8_t PREVIEW_ROW_SPACING = 4;  // Spacing between rows
    
    void drawUI();
    void drawPagePreview();
    void drawPageParameters();
    void drawPageLogs();
    void drawStatusBar();
    void drawInfoPanel();
    void drawMessageLog();
    void drawSceneNotification();
    bool hasStateChanged();
};

#endif // DISPLAY_HANDLER_H
