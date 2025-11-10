#include "midi_handler.h"
#include "led_controller.h"
#include "display_handler.h"
#include "config.h"
#include <USB.h>

MIDIHandler::MIDIHandler() 
    : _ledController(nullptr)
    , _displayHandler(nullptr)
    , _lastMessageTime(0)
{
    _lastMessage[0] = '\0';
}

void MIDIHandler::begin() {
    _midi.begin();
    USB.begin();
    updateLastMessage("MIDI Ready");
}

void MIDIHandler::setLEDController(LEDController* controller) {
    _ledController = controller;
}

void MIDIHandler::setDisplayHandler(DisplayHandler* display) {
    _displayHandler = display;
}

void MIDIHandler::update() {
    midiEventPacket_t packet;
    
    while (_midi.readPacket(&packet)) {
        uint8_t cin = packet.header & 0x0F;
        byte channel = (packet.byte1 & 0x0F) + 1;
        
        switch (cin) {
            case 0x0B: // Control Change
                handleControlChange(channel, packet.byte2, packet.byte3);
                break;
                
            case 0x09: // Note On
                if (packet.byte3 > 0) {
                    handleNoteOn(channel, packet.byte2, packet.byte3);
                } else {
                    handleNoteOff(channel, packet.byte2, 0);
                }
                break;
                
            case 0x08: // Note Off
                handleNoteOff(channel, packet.byte2, packet.byte3);
                break;
        }
    }
}

void MIDIHandler::handleControlChange(byte channel, byte controller, byte value) {
    if (!_ledController) return;
    
    char msg[32];
    snprintf(msg, 32, "CC%d=%d", controller, value);
    updateLastMessage(msg);
    
    #if DEBUG_MODE
    Serial.printf("MIDI CC - Ch:%d CC:%d Val:%d\n", channel, controller, value);
    #endif
    
    // Only respond to our MIDI channel
    if (channel != MIDI_CHANNEL) return;
    
    // Route based on CC number ranges
    if (controller >= 0 && controller <= 19) {
        // Global controls
        _ledController->handleGlobalCC(controller, value);
    } else if (controller >= 20 && controller <= 39) {
        // Color A controls
        _ledController->handleColorCC(0, controller, value);
    } else if (controller >= 40 && controller <= 59) {
        // Color B controls
        _ledController->handleColorCC(1, controller, value);
    }
    // CC 60+ reserved for future features
}

void MIDIHandler::handleNoteOn(byte channel, byte note, byte velocity) {
    if (!_ledController) return;
    
    char msg[32];
    snprintf(msg, 32, "Note %d ON", note);
    updateLastMessage(msg);
    
    #if DEBUG_MODE
    Serial.printf("MIDI Note ON - Ch:%d Note:%d Vel:%d\n", channel, note, velocity);
    #endif
    
    _ledController->handleNoteOn(note, velocity);
}

void MIDIHandler::handleNoteOff(byte channel, byte note, byte velocity) {
    if (!_ledController) return;
    
    #if DEBUG_MODE
    Serial.printf("MIDI Note OFF - Ch:%d Note:%d\n", channel, note);
    #endif
    
    _ledController->handleNoteOff(note);
}

void MIDIHandler::updateLastMessage(const char* msg) {
    strncpy(_lastMessage, msg, 31);
    _lastMessage[31] = '\0';
    _lastMessageTime = millis();
    
    if (_displayHandler) {
        _displayHandler->logMessage(msg);
    }
}
