#include "midi_processor.h"

MidiProcessor::MidiProcessor()
    : _dmxState(nullptr)
    , _displayHandler(nullptr)
    , _lastMessageTime(0) {
    _lastMessage[0] = '\0';
}

void MidiProcessor::setDMXState(DMXState* state) {
    _dmxState = state;
}

void MidiProcessor::setDisplayHandler(DisplayHandler* display) {
    _displayHandler = display;
}

void MidiProcessor::handleControlChange(uint8_t channel, uint8_t controller, uint8_t value) {
    if (!_dmxState) {
        return;
    }

    char msg[32];
    snprintf(msg, sizeof(msg), "CC%d=%d", controller, value);
    logMessage(msg);

    if (!isActiveChannel(channel)) {
        return;
    }

    if (controller >= CC_COLOR_A_HUE && controller <= CC_COLOR_A_WHITE) {
        _dmxState->handleColorCC(0, controller, value);
        return;
    }

    if (controller >= CC_COLOR_B_HUE && controller <= CC_COLOR_B_WHITE) {
        _dmxState->handleColorCC(1, controller, value);
        return;
    }

    _dmxState->handleGlobalCC(controller, value);
}

void MidiProcessor::handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (!_dmxState || !isActiveChannel(channel)) {
        return;
    }

    char msg[32];
    snprintf(msg, sizeof(msg), "Note %d ON", note);
    logMessage(msg);

    DMXState::SceneEvent event = _dmxState->handleNoteOn(note, velocity);
    if (_displayHandler && event.triggered) {
        if (event.blackout) {
            _displayHandler->logMessage("Blackout");
        } else {
            _displayHandler->showSceneNotification(event.sceneIndex, event.saved);
        }
    }
}

void MidiProcessor::handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (!_dmxState || !isActiveChannel(channel)) {
        return;
    }
    _dmxState->handleNoteOff(note);
}

void MidiProcessor::postStatusMessage(const char* message) {
    if (!message) {
        return;
    }
    logMessage(message);
}

void MidiProcessor::logMessage(const char* msg) {
    strncpy(_lastMessage, msg, sizeof(_lastMessage) - 1);
    _lastMessage[sizeof(_lastMessage) - 1] = '\0';
    _lastMessageTime = millis();

    if (_displayHandler) {
        _displayHandler->logMessage(msg);
    }
}

bool MidiProcessor::isActiveChannel(uint8_t channel) const {
    return channel == MIDI_CHANNEL;
}
