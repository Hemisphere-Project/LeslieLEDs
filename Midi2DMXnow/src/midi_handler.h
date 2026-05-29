#ifndef MIDI_HANDLER_H
#define MIDI_HANDLER_H

#include <Arduino.h>
#include <USBMIDI.h>
#include "midi_processor.h"

// Forward declarations
class DMXState;
class DisplayHandler;
class HeartbeatCollector;

// SysEx protocol constants
#define SYSEX_MANUFACTURER_ID    0x7D  // Non-commercial/educational
#define SYSEX_MSG_STATE_DUMP     0x01  // Full state dump
#define SYSEX_MSG_RIG_HEALTH     0x02  // Slave heartbeat table

class MIDIHandler {
public:
    MIDIHandler();

    void begin();
    void update();

    void setDMXState(DMXState* state);
    void setDisplayHandler(DisplayHandler* display);
    void setHeartbeats(HeartbeatCollector* hb);

    void sendStateSysEx();
    void sendRigHealthSysEx();

    const char* getLastMessage() const { return _processor.getLastMessage(); }
    unsigned long getLastMessageTime() const { return _processor.getLastMessageTime(); }

private:
    USBMIDI _midi;
    MidiProcessor _processor;
    DMXState* _dmxState;
    HeartbeatCollector* _heartbeats;
    uint32_t _lastSysExSend;
    uint32_t _lastRigHealthSend;
    static constexpr uint32_t SYSEX_SEND_INTERVAL       = 500;  // ms
    static constexpr uint32_t RIG_HEALTH_SEND_INTERVAL  = 2000; // ms
};

#endif // MIDI_HANDLER_H
