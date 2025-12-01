#ifndef MIDI_HANDLER_H
#define MIDI_HANDLER_H

#include <Arduino.h>
#include <USBMIDI.h>
#include "midi_processor.h"

// Forward declarations
class DMXState;
class DisplayHandler;

// SysEx protocol constants
#define SYSEX_MANUFACTURER_ID 0x7D  // Non-commercial/educational
#define SYSEX_MSG_STATE_DUMP  0x01  // Full state dump message

class MIDIHandler {
public:
    MIDIHandler();
    
    void begin();
    void update();
    
    // Set references to other components
    void setDMXState(DMXState* state);
    void setDisplayHandler(DisplayHandler* display);
    
    // Send current state as SysEx (call after scene load or periodically)
    void sendStateSysEx();
    
    // Get last received message info for display
    const char* getLastMessage() const { return _processor.getLastMessage(); }
    unsigned long getLastMessageTime() const { return _processor.getLastMessageTime(); }

private:
    USBMIDI _midi;
    MidiProcessor _processor;
    DMXState* _dmxState;
    uint32_t _lastSysExSend;
    static constexpr uint32_t SYSEX_SEND_INTERVAL = 500; // ms
};

#endif // MIDI_HANDLER_H
