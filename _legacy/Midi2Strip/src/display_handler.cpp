#include "display_handler.h"
#include "led_controller.h"

DisplayHandler::DisplayHandler()
    : _ledController(nullptr)
    , _logIndex(0)
    , _lastUpdate(0)
    , _sceneNotificationEnd(0)
    , _sceneNotificationNumber(0)
    , _sceneNotificationIsSave(false)
    , _needsFullRedraw(true)
    , _currentPage(0)
    , _lastPreviewUpdate(0)
{
    for (int i = 0; i < MIDI_LOG_LINES; i++) {
        _logEntries[i].text[0] = '\0';
        _logEntries[i].timestamp = 0;
    }
    
    // Initialize last state
    _lastState.mode = ANIM_SOLID;
    _lastState.brightness = 0;
    _lastState.fps = 0;
    _lastState.speed = 0;
    _lastState.lastLogIndex = -1;
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
    
    // Check if we should show scene notification
    if (now < _sceneNotificationEnd) {
        drawSceneNotification();
        return;
    }
    
    // If we just finished showing notification, force full redraw
    if (_sceneNotificationEnd > 0 && now >= _sceneNotificationEnd) {
        _sceneNotificationEnd = 0;
        _needsFullRedraw = true;
    }
    
    // Page 0 (Preview) updates faster
    if (_currentPage == 0) {
        // Preview page: 10-15 FPS
        if (now - _lastPreviewUpdate >= 66) {  // ~15 FPS
            _lastPreviewUpdate = now;
            drawUI();
        }
    } else {
        // Other pages: only update on state change
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
    _needsFullRedraw = true;  // Force redraw on new message
    #endif
}

void DisplayHandler::showSceneNotification(uint8_t sceneNumber, bool isSave) {
    #if DISPLAY_ENABLED
    _sceneNotificationNumber = sceneNumber;
    _sceneNotificationIsSave = isSave;
    _sceneNotificationEnd = millis() + 1000;  // Show for 1 second
    drawSceneNotification();  // Draw immediately
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
    if (!_ledController) return;
    
    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();
    
    // Only clear screen on first draw (page switch)
    if (_needsFullRedraw) {
        M5.Display.fillScreen(COLOR_BG);
        
        // Title
        M5.Display.setTextColor(COLOR_TITLE, COLOR_BG);
        M5.Display.setTextSize(1);
        M5.Display.setCursor(2, 2);
        M5.Display.print("LED Preview");
        
        _needsFullRedraw = false;
    }
    
    // Get LED data from controller
    const CRGB* leds = _ledController->getLEDs();
    if (!leds) return;
    
    // Calculate snake layout
    const int pixelSize = PREVIEW_PIXEL_SIZE;
    const int rowSpacing = PREVIEW_ROW_SPACING;
    const int startY = 14;
    const int availableHeight = h - startY - 2;
    const int ledsPerRow = w / pixelSize;
    const int rowHeight = pixelSize + rowSpacing;
    const int maxRows = availableHeight / rowHeight;
    const int maxLedsToShow = ledsPerRow * maxRows;
    
    int ledIndex = 0;
    int row = 0;
    int lastRowEndCol = -1;  // Track where previous row ended for connector
    
    while (ledIndex < LED_COUNT && ledIndex < maxLedsToShow && row < maxRows) {
        bool reverseRow = (row % 2 == 1);  // Snake pattern: odd rows go right-to-left
        int rowStartLed = ledIndex;
        int colCount = 0;
        
        for (int col = 0; col < ledsPerRow && ledIndex < LED_COUNT; col++) {
            int displayCol = reverseRow ? (ledsPerRow - 1 - col) : col;
            int x = displayCol * pixelSize;
            int y = startY + (row * rowHeight);
            
            // Get LED color
            CRGB color = leds[ledIndex];
            uint16_t color565 = M5.Display.color565(color.r, color.g, color.b);
            
            // Draw pixel
            M5.Display.fillRect(x, y, pixelSize, pixelSize, color565);
            
            ledIndex++;
            colCount++;
        }
        
        // Draw connector to next row (if not the last row and next row exists)
        if (row < maxRows - 1 && ledIndex < LED_COUNT) {
            // Determine which end the connector should be at
            int connectorX;
            if (reverseRow) {
                // Current row goes right-to-left, ended at left, connector at left
                connectorX = 0;
            } else {
                // Current row goes left-to-right, ended at right, connector at right
                connectorX = (colCount - 1) * pixelSize;
            }
            
            int connectorY = startY + (row * rowHeight) + pixelSize;
            
            // Use the color of the last LED in current row for the connector
            CRGB connectorColor = leds[ledIndex - 1];
            uint16_t connectorColor565 = M5.Display.color565(connectorColor.r, connectorColor.g, connectorColor.b);
            
            // Draw vertical connector in the spacing between rows
            M5.Display.fillRect(connectorX, connectorY, pixelSize, rowSpacing, connectorColor565);
        }
        
        row++;
    }
    
    // Show LED count and FPS at bottom (only redraw if FPS changed)
    static uint32_t lastDisplayedFPS = 0;
    uint32_t currentFPS = _ledController->getFPS();
    if (currentFPS != lastDisplayedFPS || _needsFullRedraw) {
        // Clear just the bottom info area
        M5.Display.fillRect(0, h - 12, w, 12, COLOR_BG);
        M5.Display.setTextColor(COLOR_TEXT, COLOR_BG);
        M5.Display.setCursor(2, h - 10);
        M5.Display.printf("%d/%d LEDs | %dFPS", 
            ledIndex < LED_COUNT ? ledIndex : LED_COUNT, 
            LED_COUNT, 
            currentFPS);
        lastDisplayedFPS = currentFPS;
    }
}

void DisplayHandler::drawPageParameters() {
    if (!_ledController) return;
    
    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();
    
    // Clear and draw title
    M5.Display.fillScreen(COLOR_BG);
    M5.Display.setTextColor(COLOR_TITLE, COLOR_BG);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(2, 2);
    M5.Display.print("Parameters");
    
    // Draw all parameter info
    drawStatusBar();
    drawInfoPanel();
}

void DisplayHandler::drawPageLogs() {
    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();
    
    // Clear and draw title
    M5.Display.fillScreen(COLOR_BG);
    M5.Display.setTextColor(COLOR_TITLE, COLOR_BG);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(2, 2);
    M5.Display.print("MIDI Log");
    
    // Draw log messages
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
    
    // Info panel (24-52px) - moved separator down
    M5.Display.fillRect(0, 24, w, 28, COLOR_BG);
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
    
    // Speed (moved down to avoid clipping)
    M5.Display.setCursor(2, 46);
    M5.Display.printf("Spd:%d", _ledController->getAnimationSpeed());
}

void DisplayHandler::drawMessageLog() {
    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();
    
    // Separator
    M5.Display.drawFastHLine(0, 58, w, COLOR_TEXT);
    
    // Message log
    int logStartY = 62;
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

void DisplayHandler::drawSceneNotification() {
    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();
    
    // Clear screen with different background for save vs load
    uint16_t bgColor = _sceneNotificationIsSave ? 
        M5.Display.color565(0, 80, 0) :      // Dark green for SAVE
        M5.Display.color565(0, 0, 80);       // Dark blue for LOAD
    
    M5.Display.fillScreen(bgColor);
    
    // Draw large scene number in center
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE, bgColor);
    
    char sceneText[16];
    snprintf(sceneText, 16, "SCENE %d", _sceneNotificationNumber + 1);
    
    // Center the text
    int16_t textWidth = strlen(sceneText) * 18;  // Approximate width for size 3
    int16_t x = (w - textWidth) / 2;
    int16_t y = (h / 2) - 24;
    
    M5.Display.setCursor(x, y);
    M5.Display.println(sceneText);
    
    // Draw action text below
    M5.Display.setTextSize(2);
    const char* actionText = _sceneNotificationIsSave ? "SAVED" : "LOADED";
    uint16_t actionColor = _sceneNotificationIsSave ? 
        M5.Display.color565(0, 255, 0) :     // Bright green for SAVED
        M5.Display.color565(100, 200, 255);  // Cyan for LOADED
    
    M5.Display.setTextColor(actionColor, bgColor);
    
    int16_t actionWidth = strlen(actionText) * 12;  // Approximate width for size 2
    x = (w - actionWidth) / 2;
    y = (h / 2) + 10;
    
    M5.Display.setCursor(x, y);
    M5.Display.println(actionText);
}

bool DisplayHandler::hasStateChanged() {
    if (!_ledController) return false;
    
    bool changed = false;
    
    // Check animation mode
    AnimationMode currentMode = _ledController->getCurrentMode();
    if (currentMode != _lastState.mode) {
        _lastState.mode = currentMode;
        changed = true;
    }
    
    // Check brightness
    uint8_t currentBrightness = _ledController->getMasterBrightness();
    if (currentBrightness != _lastState.brightness) {
        _lastState.brightness = currentBrightness;
        changed = true;
    }
    
    // Check FPS
    uint32_t currentFPS = _ledController->getFPS();
    if (currentFPS != _lastState.fps) {
        _lastState.fps = currentFPS;
        changed = true;
    }
    
    // Check Color A
    const ColorRGBW& currentColorA = _ledController->getColorA();
    if (currentColorA.r != _lastState.colorA_r || 
        currentColorA.g != _lastState.colorA_g || 
        currentColorA.b != _lastState.colorA_b || 
        currentColorA.w != _lastState.colorA_w) {
        _lastState.colorA_r = currentColorA.r;
        _lastState.colorA_g = currentColorA.g;
        _lastState.colorA_b = currentColorA.b;
        _lastState.colorA_w = currentColorA.w;
        changed = true;
    }
    
    // Check Color B
    const ColorRGBW& currentColorB = _ledController->getColorB();
    if (currentColorB.r != _lastState.colorB_r || 
        currentColorB.g != _lastState.colorB_g || 
        currentColorB.b != _lastState.colorB_b || 
        currentColorB.w != _lastState.colorB_w) {
        _lastState.colorB_r = currentColorB.r;
        _lastState.colorB_g = currentColorB.g;
        _lastState.colorB_b = currentColorB.b;
        _lastState.colorB_w = currentColorB.w;
        changed = true;
    }
    
    // Check speed
    uint8_t currentSpeed = _ledController->getAnimationSpeed();
    if (currentSpeed != _lastState.speed) {
        _lastState.speed = currentSpeed;
        changed = true;
    }
    
    // Check for new log messages
    if (_logIndex != _lastState.lastLogIndex) {
        _lastState.lastLogIndex = _logIndex;
        changed = true;
    }
    
    return changed;
}
