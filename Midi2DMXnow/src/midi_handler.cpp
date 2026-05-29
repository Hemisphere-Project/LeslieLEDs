#include "midi_handler.h"
#include "dmx_state.h"
#include "display_handler.h"
#include "heartbeat_collector.h"
#include "config.h"
#include <USB.h>

MIDIHandler::MIDIHandler()
    : _dmxState(nullptr), _heartbeats(nullptr),
      _lastSysExSend(0), _lastRigHealthSend(0) {}

void MIDIHandler::begin() {
    // Set USB device name before starting
    USB.productName(MIDI_DEVICE_NAME);
    USB.manufacturerName("LeslieLEDs");
    
    _midi.begin();
    USB.begin();
    _processor.postStatusMessage("MIDI Ready");
}

void MIDIHandler::setDMXState(DMXState* state) {
    _dmxState = state;
    _processor.setDMXState(state);
}

void MIDIHandler::setDisplayHandler(DisplayHandler* display) {
    _processor.setDisplayHandler(display);
}

void MIDIHandler::setHeartbeats(HeartbeatCollector* hb) {
    _heartbeats = hb;
}

void MIDIHandler::sendStateSysEx() {
    if (!_dmxState) return;
    
    // Build SysEx message: F0 7D 01 <17 bytes of data> F7
    // Total 20 bytes
    uint8_t sysex[20];
    sysex[0] = 0xF0;  // SysEx start
    sysex[1] = SYSEX_MANUFACTURER_ID;
    sysex[2] = SYSEX_MSG_STATE_DUMP;
    
    // Pack state data (values must be 0-127 for MIDI, scale 255->127)
    sysex[3] = _dmxState->getMasterBrightness() >> 1;  // CC 1
    sysex[4] = _dmxState->getAnimationSpeed() >> 1;    // CC 2
    sysex[5] = _dmxState->getAnimationCtrl() >> 1;     // CC 3
    sysex[6] = _dmxState->getStrobeRate() >> 1;        // CC 4
    sysex[7] = _dmxState->getBlendMode() >> 1;         // CC 5
    sysex[8] = _dmxState->getMirror() >> 1;            // CC 6
    sysex[9] = _dmxState->getDirection() >> 1;         // CC 7
    sysex[10] = static_cast<uint8_t>(_dmxState->getCurrentMode());  // CC 8 (already 0-9)
    
    const HSVColor& colorA = _dmxState->getColorA();
    sysex[11] = colorA.hue >> 1;         // CC 20
    sysex[12] = colorA.saturation >> 1;  // CC 21
    sysex[13] = colorA.value >> 1;       // CC 22
    sysex[14] = colorA.white >> 1;       // CC 23
    
    const HSVColor& colorB = _dmxState->getColorB();
    sysex[15] = colorB.hue >> 1;         // CC 30
    sysex[16] = colorB.saturation >> 1;  // CC 31
    sysex[17] = colorB.value >> 1;       // CC 32
    sysex[18] = colorB.white >> 1;       // CC 33
    
    // Active scene (0-19, or 127 for none)
    int8_t scene = _dmxState->getCurrentScene();
    sysex[19] = (scene >= 0 && scene < MAX_SCENES) ? scene : 127;
    
    // Note: SysEx end (F7) is handled by sending byte-by-byte
    // The USBMIDI write() method handles SysEx parsing
    for (int i = 0; i < 20; i++) {
        _midi.write(sysex[i]);
    }
    _midi.write(0xF7);  // SysEx end
}

void MIDIHandler::sendRigHealthSysEx() {
    if (!_heartbeats) return;

    // Per-slot payload: nid[0..2] (masked to 7 bits), status (0-4),
    // fps (0-127), resetReason (0-127).  6 bytes × 8 slots max = 48 bytes.
    // Full message: F0 7D 02 <count> [slots...] F7
    HeartbeatCollector::Slot slots[HeartbeatCollector::MAX_SLAVES];
    _heartbeats->copySlots(slots);  // thread-safe snapshot
    const uint32_t now = millis();

    auto statusFromSnapshot = [now](const HeartbeatCollector::Slot& slot) {
        if (!slot.used) return HeartbeatCollector::EMPTY;

        const uint32_t age = now - slot.lastHeardLocalMs;
        if (age >= HeartbeatCollector::LOST_MS) return HeartbeatCollector::LOST;
        if (age >= HeartbeatCollector::STALE_MS) return HeartbeatCollector::STALE;
        if (slot.last.msSinceLastFrame >= HeartbeatCollector::NO_DMX_FRAME_MS) {
            return HeartbeatCollector::NO_DMX;
        }
        return HeartbeatCollector::OK;
    };

    uint8_t buf[4 + HeartbeatCollector::MAX_SLAVES * 6 + 1];
    buf[0] = 0xF0;
    buf[1] = SYSEX_MANUFACTURER_ID;
    buf[2] = SYSEX_MSG_RIG_HEALTH;

    uint8_t count = 0;
    uint8_t* p = buf + 4;  // reserve byte 3 for count
    for (uint8_t i = 0; i < HeartbeatCollector::MAX_SLAVES; ++i) {
        const auto& s = slots[i];
        if (!s.used) continue;
        p[0] = s.nid[0] & 0x7F;
        p[1] = s.nid[1] & 0x7F;
        p[2] = s.nid[2] & 0x7F;
        p[3] = static_cast<uint8_t>(statusFromSnapshot(s));
        p[4] = s.last.fps & 0x7F;
        p[5] = s.last.lastResetReason & 0x7F;
        p += 6;
        ++count;
    }
    buf[3] = count;
    *p++ = 0xF7;

    const int totalLen = static_cast<int>(p - buf);
    for (int i = 0; i < totalLen; ++i) {
        _midi.write(buf[i]);
    }
}

void MIDIHandler::update() {
    midiEventPacket_t packet;
    
    while (_midi.readPacket(&packet)) {
        uint8_t cin = packet.header & 0x0F;
        byte channel = (packet.byte1 & 0x0F) + 1;
        
        switch (cin) {
            case 0x0B: // Control Change
                _processor.handleControlChange(channel, packet.byte2, packet.byte3);
                break;
                
            case 0x09: // Note On
                if (packet.byte3 > 0) {
                    _processor.handleNoteOn(channel, packet.byte2, packet.byte3);
                } else {
                    _processor.handleNoteOff(channel, packet.byte2, 0);
                }
                break;
                
            case 0x08: // Note Off
                _processor.handleNoteOff(channel, packet.byte2, packet.byte3);
                break;
        }
    }
    
    // Periodic SysEx state broadcast
    uint32_t now = millis();
    if (now - _lastSysExSend >= SYSEX_SEND_INTERVAL) {
        sendStateSysEx();
        _lastSysExSend = now;
    }
    if (now - _lastRigHealthSend >= RIG_HEALTH_SEND_INTERVAL) {
        sendRigHealthSysEx();
        _lastRigHealthSend = now;
    }
}
