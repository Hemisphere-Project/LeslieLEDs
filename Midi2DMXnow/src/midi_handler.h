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
#define SYSEX_MSG_SCENE_BANK_REQUEST 0x10  // Host requests the full scene bank
#define SYSEX_MSG_SCENE_BANK_DUMP    0x11  // Device returns the full scene bank
#define SYSEX_MSG_SCENE_BANK_LOAD    0x12  // Host replaces the full scene bank
#define SYSEX_MSG_SCENE_BANK_STATUS  0x13  // Device acknowledges bank load/result

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
    size_t _sysexRxLength;
    bool _sysexRxActive;
    static constexpr size_t SYSEX_RX_MAX_BYTES = 3072;
    uint8_t _sysexRxBuffer[SYSEX_RX_MAX_BYTES];
    uint8_t _sceneBankBuffer[DMXState::SCENE_BANK_WIRE_SIZE];
    static constexpr uint32_t SYSEX_SEND_INTERVAL       = 500;  // ms
    static constexpr uint32_t RIG_HEALTH_SEND_INTERVAL  = 2000; // ms

    void resetSysExReceive();
    void processSysExPacket(const midiEventPacket_t& packet, uint8_t cin);
    void appendSysExBytes(const uint8_t* data, size_t count, bool complete);
    void handleCompletedSysEx();
    void sendSceneBankDumpSysEx();
    void sendSceneBankStatusSysEx(uint8_t status, uint8_t detail = 0);
};

#endif // MIDI_HANDLER_H
