#ifndef MIDI_PROCESSOR_H
#define MIDI_PROCESSOR_H

#include <Arduino.h>
#include "config.h"
#include "dmx_state.h"
#include "display_handler.h"

/**
 * MidiProcessor centralizes MIDI -> DMX/Display routing so different
 * transport handlers (USB, Serial, etc.) can reuse the same business logic.
 */
class MidiProcessor {
public:
    MidiProcessor();

    void setDMXState(DMXState* state);
    void setDisplayHandler(DisplayHandler* display);

    void handleControlChange(uint8_t channel, uint8_t controller, uint8_t value);
    void handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);
    void postStatusMessage(const char* message);

    const char* getLastMessage() const { return _lastMessage; }
    unsigned long getLastMessageTime() const { return _lastMessageTime; }

private:
    DMXState* _dmxState;
    DisplayHandler* _displayHandler;
    char _lastMessage[32];
    unsigned long _lastMessageTime;

    void logMessage(const char* msg);
    bool isActiveChannel(uint8_t channel) const;
};

#endif // MIDI_PROCESSOR_H
