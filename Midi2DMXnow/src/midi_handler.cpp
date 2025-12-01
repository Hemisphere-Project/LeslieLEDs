#include "midi_handler.h"
#include "dmx_state.h"
#include "display_handler.h"
#include "config.h"
#include <USB.h>

MIDIHandler::MIDIHandler() : _dmxState(nullptr), _lastSysExSend(0) {}

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
}
