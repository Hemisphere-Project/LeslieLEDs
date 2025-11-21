#include "led_preview_renderer.h"

using namespace LedEngineLib;

static constexpr uint8_t PREVIEW_PIXEL_SIZE = 4;
static constexpr uint8_t PREVIEW_ROW_SPACING = 4;

LedPreviewRenderer::LedPreviewRenderer()
    : _lastDisplayedFPS(0) {}

void LedPreviewRenderer::draw(M5GFX& display, LedEngine* engine, bool forceFullRedraw) {
#if DISPLAY_ENABLED
    if (!engine) {
        return;
    }

    const int16_t w = display.width();
    const int16_t h = display.height();

    if (forceFullRedraw) {
        display.fillScreen(COLOR_BG);
        display.setTextColor(COLOR_TITLE, COLOR_BG);
        display.setTextSize(1);
        display.setCursor(2, 2);
        display.print("LED Preview");
    }

    const CRGB* leds = engine->getPreviewPixels();
    if (!leds) {
        return;
    }

    const uint16_t ledCount = engine->getLedCount();
    const int startY = 14;
    const int availableHeight = h - startY - 2;
    const int ledsPerRow = w / PREVIEW_PIXEL_SIZE;
    const int rowHeight = PREVIEW_PIXEL_SIZE + PREVIEW_ROW_SPACING;
    const int maxRows = availableHeight / rowHeight;
    const int maxLedsToShow = ledsPerRow * maxRows;

    int ledIndex = 0;
    int row = 0;

    while (ledIndex < ledCount && ledIndex < maxLedsToShow && row < maxRows) {
        const bool reverseRow = (row % 2 == 1);
        int colCount = 0;

        for (int col = 0; col < ledsPerRow && ledIndex < ledCount; col++) {
            const int displayCol = reverseRow ? (ledsPerRow - 1 - col) : col;
            const int x = displayCol * PREVIEW_PIXEL_SIZE;
            const int y = startY + (row * rowHeight);

            const CRGB& color = leds[ledIndex];
            const uint16_t color565 = display.color565(color.r, color.g, color.b);
            display.fillRect(x, y, PREVIEW_PIXEL_SIZE, PREVIEW_PIXEL_SIZE, color565);

            ledIndex++;
            colCount++;
        }

        if (row < maxRows - 1 && ledIndex < ledCount && colCount > 0) {
            const int connectorX = reverseRow ? 0 : (colCount - 1) * PREVIEW_PIXEL_SIZE;
            const int connectorY = startY + (row * rowHeight) + PREVIEW_PIXEL_SIZE;
            const CRGB& connectorColor = leds[ledIndex - 1];
            const uint16_t connector565 = display.color565(connectorColor.r, connectorColor.g, connectorColor.b);
            display.fillRect(connectorX, connectorY, PREVIEW_PIXEL_SIZE, PREVIEW_ROW_SPACING, connector565);
        }

        row++;
    }

    const uint8_t currentFPS = engine->getFPS();
    if (forceFullRedraw || currentFPS != _lastDisplayedFPS) {
        display.fillRect(0, h - 12, w, 12, COLOR_BG);
        display.setTextColor(COLOR_TEXT, COLOR_BG);
        display.setCursor(2, h - 10);
        display.printf("%d LEDs | %dFPS", ledCount, currentFPS);
        _lastDisplayedFPS = currentFPS;
    }
#endif
}
