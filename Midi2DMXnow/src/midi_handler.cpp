#include "midi_handler.h"
#include "dmx_state.h"
#include "display_handler.h"
#include "config.h"
#include <USB.h>

MIDIHandler::MIDIHandler() = default;

void MIDIHandler::begin() {
    // Set USB device name before starting
    USB.productName(MIDI_DEVICE_NAME);
    USB.manufacturerName("LeslieLEDs");
    
    _midi.begin();
    USB.begin();
    _processor.postStatusMessage("MIDI Ready");
}

void MIDIHandler::setDMXState(DMXState* state) {
    _processor.setDMXState(state);
}

void MIDIHandler::setDisplayHandler(DisplayHandler* display) {
    _processor.setDisplayHandler(display);
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
}
