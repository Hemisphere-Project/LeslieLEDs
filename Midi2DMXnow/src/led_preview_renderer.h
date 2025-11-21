#ifndef LED_PREVIEW_RENDERER_H
#define LED_PREVIEW_RENDERER_H

#include <M5Unified.h>
#include <LedEngine.h>
#include "config.h"

class LedPreviewRenderer {
public:
    LedPreviewRenderer();

    void draw(M5GFX& display, LedEngineLib::LedEngine* engine, bool forceFullRedraw);

private:
    uint8_t _lastDisplayedFPS;
};

#endif // LED_PREVIEW_RENDERER_H
