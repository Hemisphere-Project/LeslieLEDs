#include "display_handler.h"

DisplayHandler::DisplayHandler()
    : _ledEngine(nullptr)
    , _dmxState(nullptr)
    , _heartbeats(nullptr)
    , _lastUpdate(0)
    , _sceneNotificationEnd(0)
    , _sceneNotificationNumber(0)
    , _sceneNotificationIsSave(false)
    , _needsFullRedraw(true)
    , _currentScene(-1)
    , _lastPreviewUpdate(0) {
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

    // Scene notification takes priority
    if (now < _sceneNotificationEnd) {
        drawSceneNotification();
        return;
    }

    // Clear notification state and force redraw when notification ends
    if (_sceneNotificationEnd > 0 && now >= _sceneNotificationEnd) {
        _sceneNotificationEnd = 0;
        _needsFullRedraw = true;
    }

    // Update preview at ~15fps
    if (now - _lastPreviewUpdate >= 66) {
        _lastPreviewUpdate = now;
        drawPreview();
    }
#endif
}

void DisplayHandler::logMessage(const char* message) {
#if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
    Serial.printf("[LOG] %s\n", message);
#else
    (void)message;
#endif
}

void DisplayHandler::showSceneNotification(uint8_t sceneNumber, bool isSave) {
#if DISPLAY_ENABLED
    _sceneNotificationNumber = sceneNumber;
    _sceneNotificationIsSave = isSave;
    _sceneNotificationEnd = millis() + 800;  // Slightly shorter notification
    _currentScene = sceneNumber;  // Track current scene
    drawSceneNotification();
#endif
}

void DisplayHandler::handleButtonPress() {
    if (!_dmxState) return;
    
    // Rotate to next scene (0-9), wrapping around
    int8_t nextScene = (_currentScene + 1) % MAX_SCENES;
    
    // Load the scene via DMXState (simulating a note-on for scene load)
    byte sceneNote = NOTE_SCENE_1 + nextScene;
    DMXState::SceneEvent event = _dmxState->handleNoteOn(sceneNote, 127);
    
    if (event.triggered) {
        _currentScene = nextScene;
        showSceneNotification(nextScene, false);
    }
}

void DisplayHandler::drawPreview() {
#if DISPLAY_ENABLED
    if (!_ledEngine) return;

    _previewRenderer.draw(M5.Display, _ledEngine, _needsFullRedraw);

    // Draw scene indicator on top of preview
    drawSceneIndicator();

    // Rig health dot row (slaves heard recently, one coloured dot each)
    drawHeartbeatDots();

    _needsFullRedraw = false;
#endif
}

void DisplayHandler::drawHeartbeatDots() {
#if DISPLAY_ENABLED
    if (!_heartbeats) return;

    // Position: just above the "LED count / FPS" footer so the status row
    // doesn't collide with the title bar or scene indicator.
    const int16_t w = M5.Display.width();
    const int16_t h = M5.Display.height();
    const int16_t y = h - 20;
    const int16_t dotW = 8;
    const int16_t dotH = 4;
    const int16_t gap = 2;
    const uint8_t maxDots = 5;
    const int16_t rowW = (maxDots * dotW) + ((maxDots - 1) * gap);
    const int16_t x = w - rowW - 2;

    M5.Display.fillRect(x - 1, y - 1, rowW + 2, dotH + 2, COLOR_BG);

    uint32_t now = millis();
    int16_t drawX = x;
    for (uint8_t i = 0; i < maxDots; i++) {
        HeartbeatCollector::Status st = _heartbeats->statusOf(i, now);
        uint16_t colour;
        switch (st) {
            case HeartbeatCollector::OK:     colour = M5.Display.color565(0,   200, 0);   break;
            case HeartbeatCollector::NO_DMX: colour = M5.Display.color565(220, 120, 0);   break;
            case HeartbeatCollector::STALE:  colour = M5.Display.color565(220, 180, 0);   break;
            case HeartbeatCollector::LOST:   colour = M5.Display.color565(220, 30,  30);  break;
            case HeartbeatCollector::EMPTY:
            default:                         colour = M5.Display.color565(40,  40,  40);  break;
        }
        M5.Display.fillRect(drawX, y, dotW, dotH, colour);
        drawX += dotW + gap;
    }
#endif
}

void DisplayHandler::drawSceneIndicator() {
#if DISPLAY_ENABLED
    // Show current scene in top-right corner
    int16_t w = M5.Display.width();
    
    // Background box for scene indicator
    M5.Display.fillRect(w - 28, 2, 26, 12, COLOR_BG);
    
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COLOR_STATE_OK, COLOR_BG);
    M5.Display.setCursor(w - 26, 4);
    
    if (_currentScene >= 0) {
        M5.Display.printf("S%d", _currentScene + 1);
    } else {
        M5.Display.print("--");
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
