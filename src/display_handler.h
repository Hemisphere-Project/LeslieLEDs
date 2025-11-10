#ifndef DISPLAY_HANDLER_H
#define DISPLAY_HANDLER_H

#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"

// Forward declaration
class LEDController;

class DisplayHandler {
public:
    DisplayHandler();
    
    void begin();
    void update();
    
    void setLEDController(LEDController* controller);
    void logMessage(const char* message);
    
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
    
    void drawUI();
    void drawStatusBar();
    void drawInfoPanel();
    void drawMessageLog();
};

#endif // DISPLAY_HANDLER_H
