#include "display_handler.h"
#include "led_controller.h"

DisplayHandler::DisplayHandler()
    : _ledController(nullptr)
    , _logIndex(0)
    , _lastUpdate(0)
{
    for (int i = 0; i < MIDI_LOG_LINES; i++) {
        _logEntries[i].text[0] = '\0';
        _logEntries[i].timestamp = 0;
    }
}

void DisplayHandler::begin() {
    #if DISPLAY_ENABLED
    M5.Display.setBrightness(DISPLAY_BRIGHTNESS);
    M5.Display.fillScreen(COLOR_BG);
    M5.Display.setTextSize(1);
    
    // Draw title
    M5.Display.setTextColor(COLOR_TITLE, COLOR_BG);
    M5.Display.setCursor(2, 2);
    M5.Display.println("LeslieLEDs");
    
    logMessage("System Ready");
    #endif
}

void DisplayHandler::setLEDController(LEDController* controller) {
    _ledController = controller;
}

void DisplayHandler::update() {
    #if DISPLAY_ENABLED
    unsigned long now = millis();
    if (now - _lastUpdate < DISPLAY_UPDATE_MS) {
        return;
    }
    _lastUpdate = now;
    
    drawUI();
    #endif
}

void DisplayHandler::logMessage(const char* message) {
    #if DISPLAY_ENABLED
    strncpy(_logEntries[_logIndex].text, message, 31);
    _logEntries[_logIndex].text[31] = '\0';
    _logEntries[_logIndex].timestamp = millis();
    _logIndex = (_logIndex + 1) % MIDI_LOG_LINES;
    #endif
}

void DisplayHandler::drawUI() {
    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();
    
    // Title bar (top 12px)
    M5.Display.fillRect(0, 0, w, 12, COLOR_BG);
    M5.Display.setTextColor(COLOR_TITLE, COLOR_BG);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(2, 2);
    M5.Display.print("LeslieLEDs");
    
    drawStatusBar();
    drawInfoPanel();
    drawMessageLog();
}

void DisplayHandler::drawStatusBar() {
    if (!_ledController) return;
    
    int16_t w = M5.Display.width();
    
    // Status line (12-24px)
    M5.Display.fillRect(0, 12, w, 12, COLOR_BG);
    M5.Display.setTextColor(COLOR_STATE_OK, COLOR_BG);
    M5.Display.setCursor(2, 14);
    
    // Show mode and brightness
    const char* modeNames[] = {
        "SOLID", "DUAL", "CHASE", "DASH", "STRB",
        "PULSE", "RNBW", "SPKL", "CST1", "CST2"
    };
    
    AnimationMode mode = _ledController->getCurrentMode();
    if (mode < ANIM_MODE_COUNT) {
        M5.Display.print(modeNames[mode]);
    }
    
    M5.Display.printf(" B:%d", _ledController->getMasterBrightness());
    M5.Display.printf(" %dFPS", _ledController->getFPS());
}

void DisplayHandler::drawInfoPanel() {
    if (!_ledController) return;
    
    int16_t w = M5.Display.width();
    
    // Info panel (24-50px)
    M5.Display.fillRect(0, 24, w, 26, COLOR_BG);
    M5.Display.setTextColor(COLOR_TEXT, COLOR_BG);
    M5.Display.setTextSize(1);
    
    // Color A
    M5.Display.setCursor(2, 26);
    M5.Display.print("A:");
    const ColorRGBW& colorA = _ledController->getColorA();
    M5.Display.fillRect(14, 26, 20, 8, M5.Display.color565(colorA.r, colorA.g, colorA.b));
    M5.Display.printf(" %d,%d,%d,%d", colorA.r, colorA.g, colorA.b, colorA.w);
    
    // Color B
    M5.Display.setCursor(2, 36);
    M5.Display.print("B:");
    const ColorRGBW& colorB = _ledController->getColorB();
    M5.Display.fillRect(14, 36, 20, 8, M5.Display.color565(colorB.r, colorB.g, colorB.b));
    M5.Display.printf(" %d,%d,%d,%d", colorB.r, colorB.g, colorB.b, colorB.w);
    
    // Speed
    M5.Display.setCursor(2, 46);
    M5.Display.printf("Spd:%d", _ledController->getAnimationSpeed());
}

void DisplayHandler::drawMessageLog() {
    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();
    
    // Separator
    M5.Display.drawFastHLine(0, 52, w, COLOR_TEXT);
    
    // Message log (54px to bottom)
    int logStartY = 54;
    int lineHeight = 10;
    
    M5.Display.fillRect(0, logStartY, w, h - logStartY, COLOR_BG);
    M5.Display.setTextColor(COLOR_MIDI_CC, COLOR_BG);
    M5.Display.setTextSize(1);
    
    // Display recent messages (reverse order)
    int displayLine = 0;
    for (int i = 0; i < MIDI_LOG_LINES && displayLine < 7; i++) {
        int logIdx = (_logIndex - 1 - i + MIDI_LOG_LINES) % MIDI_LOG_LINES;
        if (_logEntries[logIdx].text[0] != '\0') {
            int y = logStartY + (displayLine * lineHeight);
            if (y + lineHeight <= h) {
                M5.Display.setCursor(2, y);
                M5.Display.print(_logEntries[logIdx].text);
                displayLine++;
            }
        }
    }
}
