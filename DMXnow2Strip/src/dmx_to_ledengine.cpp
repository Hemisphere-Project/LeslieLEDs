#include "dmx_to_ledengine.h"

using namespace LedEngineLib;

namespace {

MirrorMode decodeMirror(uint8_t value) {
    if (value < 51) return MIRROR_NONE;
    if (value < 102) return MIRROR_FULL;
    if (value < 153) return MIRROR_SPLIT2;
    if (value < 204) return MIRROR_SPLIT3;
    return MIRROR_SPLIT4;
}

DirectionMode decodeDirection(uint8_t value) {
    if (value < 64) return DIR_FORWARD;
    if (value < 128) return DIR_BACKWARD;
    if (value < 192) return DIR_PINGPONG;
    return DIR_RANDOM;
}

}

DMXToLedEngine::DMXToLedEngine()
    : _state()
    , _hasState(false)
    , _colorA_H(0), _colorA_S(255), _colorA_V(255), _colorA_W(0)
    , _colorB_H(160), _colorB_S(255), _colorB_V(255), _colorB_W(0) {
}

void DMXToLedEngine::applyDMXFrame(const uint8_t* dmxData, uint16_t size) {
    if (size < 16) {
        return;
    }

    _state.masterBrightness = dmxData[DMX_CH_MASTER_BRIGHTNESS];

    uint8_t modeValue = dmxData[DMX_CH_ANIMATION_MODE] / 25;
    if (modeValue >= ANIM_MODE_COUNT) {
        modeValue = ANIM_MODE_COUNT - 1;
    }
    _state.mode = static_cast<AnimationMode>(modeValue);

    _state.animationSpeed = dmxData[DMX_CH_ANIMATION_SPEED];
    _state.animationCtrl = dmxData[DMX_CH_ANIMATION_CTRL];
    _state.strobeRate = dmxData[DMX_CH_STROBE_RATE];
    _state.blendMode = dmxData[DMX_CH_BLEND_MODE];
    _state.mirror = decodeMirror(dmxData[DMX_CH_MIRROR_MODE]);
    _state.direction = decodeDirection(dmxData[DMX_CH_DIRECTION]);

    _colorA_H = dmxData[DMX_CH_COLOR_A_HUE];
    _colorA_S = dmxData[DMX_CH_COLOR_A_SATURATION];
    _colorA_V = dmxData[DMX_CH_COLOR_A_VALUE];
    _colorA_W = dmxData[DMX_CH_COLOR_A_WHITE];

    _state.colorA.fromHSV(_colorA_H, _colorA_S, _colorA_V, _colorA_W);

    _colorB_H = dmxData[DMX_CH_COLOR_B_HUE];
    _colorB_S = dmxData[DMX_CH_COLOR_B_SATURATION];
    _colorB_V = dmxData[DMX_CH_COLOR_B_VALUE];
    _colorB_W = dmxData[DMX_CH_COLOR_B_WHITE];

    _state.colorB.fromHSV(_colorB_H, _colorB_S, _colorB_V, _colorB_W);

    _hasState = true;
}
