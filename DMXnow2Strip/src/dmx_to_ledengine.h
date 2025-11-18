#ifndef DMX_TO_LEDENGINE_H
#define DMX_TO_LEDENGINE_H

#include <Arduino.h>
#include <LedEngine.h>
#include "config.h"

/**
 * DMXToLedEngine - Adapter that maps DMX channels to LedEngine parameters
 */
class DMXToLedEngine {
public:
    DMXToLedEngine();

    void applyDMXFrame(const uint8_t* dmxData, uint16_t size);
    bool hasState() const { return _hasState; }
    const LedEngineLib::LedEngineState& getState() const { return _state; }

private:
    LedEngineLib::LedEngineState _state;
    bool _hasState;

    // Last received values (for HSV reconstruction)
    uint8_t _colorA_H, _colorA_S, _colorA_V, _colorA_W;
    uint8_t _colorB_H, _colorB_S, _colorB_V, _colorB_W;
};

#endif // DMX_TO_LEDENGINE_H
