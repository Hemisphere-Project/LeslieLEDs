#include "display_handler.h"

DisplayHandler::DisplayHandler()
    : _ledEngine(nullptr)
    , _dmxState(nullptr)
    , _logIndex(0)
    , _lastUpdate(0)
    , _sceneNotificationEnd(0)
    , _sceneNotificationNumber(0)
    , _sceneNotificationIsSave(false)
    , _needsFullRedraw(true)
    , _currentPage(0)
    , _lastPreviewUpdate(0) {
    for (int i = 0; i < MIDI_LOG_LINES; i++) {
        _logEntries[i].text[0] = '\0';
        _logEntries[i].timestamp = 0;
    }

    _lastState.mode = LedEngineLib::ANIM_SOLID;
    _lastState.brightness = 0;
    _lastState.speed = 0;
    _lastState.fps = 0;
    _lastState.colorA = LedEngineLib::ColorRGBW();
    _lastState.colorB = LedEngineLib::ColorRGBW();
    _lastState.lastLogIndex = -1;
}

void DisplayHandler::begin() {
#if DISPLAY_ENABLED
    M5.Display.setBrightness(DISPLAY_BRIGHTNESS);
    M5.Display.fillScreen(COLOR_BG);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COLOR_TITLE, COLOR_BG);
    M5.Display.setCursor(2, 2);
    M5.Display.println("Midi2DMXnow");
    logMessage("System Ready");
#endif
}

void DisplayHandler::setLedEngine(LedEngineLib::LedEngine* engine) {
    _ledEngine = engine;
}

void DisplayHandler::setDMXState(DMXState* state) {
    _dmxState = state;
}

void DisplayHandler::update() {
#if DISPLAY_ENABLED
    unsigned long now = millis();

    if (now < _sceneNotificationEnd) {
        drawSceneNotification();
        return;
    }

    if (_sceneNotificationEnd > 0 && now >= _sceneNotificationEnd) {
        _sceneNotificationEnd = 0;
        _needsFullRedraw = true;
    }

    if (_currentPage == 0) {
        if (now - _lastPreviewUpdate >= 66) {
            _lastPreviewUpdate = now;
            drawUI();
        }
    } else {
        if (now - _lastUpdate < DISPLAY_UPDATE_MS) {
            return;
        }
        _lastUpdate = now;
        if (_needsFullRedraw || hasStateChanged()) {
            drawUI();
            _needsFullRedraw = false;
        }
    }
#endif
}

void DisplayHandler::logMessage(const char* message) {
#if DISPLAY_ENABLED
    strncpy(_logEntries[_logIndex].text, message, 31);
    _logEntries[_logIndex].text[31] = '\0';
    _logEntries[_logIndex].timestamp = millis();
    _logIndex = (_logIndex + 1) % MIDI_LOG_LINES;
    _needsFullRedraw = true;
#endif
}

void DisplayHandler::showSceneNotification(uint8_t sceneNumber, bool isSave) {
#if DISPLAY_ENABLED
    _sceneNotificationNumber = sceneNumber;
    _sceneNotificationIsSave = isSave;
    _sceneNotificationEnd = millis() + 1000;
    drawSceneNotification();
#endif
}

void DisplayHandler::handleButtonPress() {
#if DISPLAY_ENABLED
    _currentPage = (_currentPage + 1) % PAGE_COUNT;
    _needsFullRedraw = true;
#endif
}

void DisplayHandler::drawUI() {
    switch (_currentPage) {
        case 0:
            drawPagePreview();
            break;
        case 1:
            drawPageParameters();
            break;
        case 2:
            drawPageLogs();
            break;
    }
}

void DisplayHandler::drawPagePreview() {
#if DISPLAY_ENABLED
    if (!_ledEngine) return;
    _previewRenderer.draw(M5.Display, _ledEngine, _needsFullRedraw);
    _needsFullRedraw = false;
#endif
}

void DisplayHandler::drawPageParameters() {
#if DISPLAY_ENABLED
    if (!_ledEngine) return;

    M5.Display.fillScreen(COLOR_BG);
    M5.Display.setTextColor(COLOR_TITLE, COLOR_BG);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(2, 2);
    M5.Display.print("Parameters");

    drawStatusBar();
    drawInfoPanel();
#endif
}

void DisplayHandler::drawPageLogs() {
#if DISPLAY_ENABLED
    M5.Display.fillScreen(COLOR_BG);
    M5.Display.setTextColor(COLOR_TITLE, COLOR_BG);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(2, 2);
    M5.Display.print("MIDI Log");
    drawMessageLog();
#endif
}

void DisplayHandler::drawStatusBar() {
#if DISPLAY_ENABLED
    if (!_dmxState || !_ledEngine) return;

    int16_t w = M5.Display.width();
    M5.Display.fillRect(0, 12, w, 12, COLOR_BG);
    M5.Display.setTextColor(COLOR_STATE_OK, COLOR_BG);
    M5.Display.setCursor(2, 14);

    const char* modeNames[] = {
        "SOLID", "DUAL", "CHASE", "DASH", "WAVE",
        "PULSE", "RNBW", "SPKL", "CST1", "CST2"
    };

    LedEngineLib::AnimationMode mode = _dmxState->getCurrentMode();
    if (mode < LedEngineLib::ANIM_MODE_COUNT) {
        M5.Display.print(modeNames[mode]);
    }

    M5.Display.printf(" B:%d", _dmxState->getMasterBrightness());
    M5.Display.printf(" %dFPS", _ledEngine->getFPS());
#endif
}

void DisplayHandler::drawInfoPanel() {
#if DISPLAY_ENABLED
    if (!_dmxState) return;

    LedEngineLib::LedEngineState state = currentEngineState();

    int16_t w = M5.Display.width();
    M5.Display.fillRect(0, 24, w, 28, COLOR_BG);
    M5.Display.setTextColor(COLOR_TEXT, COLOR_BG);
    M5.Display.setTextSize(1);

    M5.Display.setCursor(2, 26);
    M5.Display.print("A:");
    M5.Display.fillRect(14, 26, 20, 8, M5.Display.color565(state.colorA.r, state.colorA.g, state.colorA.b));
    M5.Display.printf(" %d,%d,%d,%d", state.colorA.r, state.colorA.g, state.colorA.b, state.colorA.w);

    M5.Display.setCursor(2, 36);
    M5.Display.print("B:");
    M5.Display.fillRect(14, 36, 20, 8, M5.Display.color565(state.colorB.r, state.colorB.g, state.colorB.b));
    M5.Display.printf(" %d,%d,%d,%d", state.colorB.r, state.colorB.g, state.colorB.b, state.colorB.w);

    M5.Display.setCursor(2, 46);
    M5.Display.printf("Spd:%d", _dmxState->getAnimationSpeed());
#endif
}

void DisplayHandler::drawMessageLog() {
#if DISPLAY_ENABLED
    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();
    M5.Display.drawFastHLine(0, 58, w, COLOR_TEXT);

    int logStartY = 62;
    int lineHeight = 10;

    M5.Display.fillRect(0, logStartY, w, h - logStartY, COLOR_BG);
    M5.Display.setTextColor(COLOR_MIDI_CC, COLOR_BG);
    M5.Display.setTextSize(1);

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
#endif
}

void DisplayHandler::drawSceneNotification() {
#if DISPLAY_ENABLED
    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();

    uint16_t bgColor = _sceneNotificationIsSave ?
        M5.Display.color565(0, 80, 0) :
        M5.Display.color565(0, 0, 80);

    M5.Display.fillScreen(bgColor);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE, bgColor);

    char sceneText[16];
    snprintf(sceneText, sizeof(sceneText), "SCENE %d", _sceneNotificationNumber + 1);
    int16_t textWidth = strlen(sceneText) * 18;
    int16_t x = (w - textWidth) / 2;
    int16_t y = (h / 2) - 24;
    M5.Display.setCursor(x, y);
    M5.Display.println(sceneText);

    M5.Display.setTextSize(2);
    const char* actionText = _sceneNotificationIsSave ? "SAVED" : "LOADED";
    uint16_t actionColor = _sceneNotificationIsSave ?
        M5.Display.color565(0, 255, 0) :
        M5.Display.color565(100, 200, 255);
    int16_t actionWidth = strlen(actionText) * 12;
    x = (w - actionWidth) / 2;
    y = (h / 2) + 10;
    M5.Display.setTextColor(actionColor, bgColor);
    M5.Display.setCursor(x, y);
    M5.Display.println(actionText);
#endif
}

bool DisplayHandler::hasStateChanged() {
    if (!_dmxState || !_ledEngine) return false;

    bool changed = false;
    LedEngineLib::LedEngineState state = currentEngineState();

    if (state.mode != _lastState.mode) {
        _lastState.mode = state.mode;
        changed = true;
    }

    if (state.masterBrightness != _lastState.brightness) {
        _lastState.brightness = state.masterBrightness;
        changed = true;
    }

    if (state.animationSpeed != _lastState.speed) {
        _lastState.speed = state.animationSpeed;
        changed = true;
    }

    if (state.colorA.r != _lastState.colorA.r ||
        state.colorA.g != _lastState.colorA.g ||
        state.colorA.b != _lastState.colorA.b ||
        state.colorA.w != _lastState.colorA.w) {
        _lastState.colorA = state.colorA;
        changed = true;
    }

    if (state.colorB.r != _lastState.colorB.r ||
        state.colorB.g != _lastState.colorB.g ||
        state.colorB.b != _lastState.colorB.b ||
        state.colorB.w != _lastState.colorB.w) {
        _lastState.colorB = state.colorB;
        changed = true;
    }

    uint8_t fps = _ledEngine->getFPS();
    if (fps != _lastState.fps) {
        _lastState.fps = fps;
        changed = true;
    }

    if (_logIndex != _lastState.lastLogIndex) {
        _lastState.lastLogIndex = _logIndex;
        changed = true;
    }

    return changed;
}

LedEngineLib::LedEngineState DisplayHandler::currentEngineState() const {
    if (_dmxState) {
        return _dmxState->toLedEngineState();
    }
    if (_ledEngine) {
        return _ledEngine->getState();
    }
    return LedEngineLib::LedEngineState();
}
