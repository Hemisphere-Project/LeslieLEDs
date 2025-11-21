#ifndef MIDI_HANDLER_H
#define MIDI_HANDLER_H

#include <Arduino.h>
#include <USBMIDI.h>
#include "midi_processor.h"

// Forward declarations
class DMXState;
class DisplayHandler;

class MIDIHandler {
public:
    MIDIHandler();
    
    void begin();
    void update();
    
    // Set references to other components
    void setDMXState(DMXState* state);
    void setDisplayHandler(DisplayHandler* display);
    
    // Get last received message info for display
    const char* getLastMessage() const { return _processor.getLastMessage(); }
    unsigned long getLastMessageTime() const { return _processor.getLastMessageTime(); }

private:
    USBMIDI _midi;
    MidiProcessor _processor;
};

#endif // MIDI_HANDLER_H
