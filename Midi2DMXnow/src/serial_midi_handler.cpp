#include "serial_midi_handler.h"

#define CONNECTION_TIMEOUT_MS 5000

SerialMIDIHandler::SerialMIDIHandler() :
    bufferIndex(0),
    expectedBytes(0),
    runningStatus(0),
    ccCallback(nullptr),
    noteOnCallback(nullptr),
    noteOffCallback(nullptr),
    lastMessageTime(0),
    connected(false) {
}

void SerialMIDIHandler::begin() {
#ifdef USE_SERIAL_MIDI
    Serial.begin(SERIAL_MIDI_BAUD);
    // Don't print anything - Serial is used for MIDI data only!
    delay(100);
#endif
}

void SerialMIDIHandler::setDMXState(DMXState* state) {
    _processor.setDMXState(state);
}

void SerialMIDIHandler::setDisplayHandler(DisplayHandler* display) {
    _processor.setDisplayHandler(display);
}

void SerialMIDIHandler::update() {
#ifdef USE_SERIAL_MIDI
    // Check connection status
    if (connected && (millis() - lastMessageTime > CONNECTION_TIMEOUT_MS)) {
        connected = false;
    }
    
    // Process incoming MIDI bytes
    while (Serial.available() > 0) {
        uint8_t byte = Serial.read();
        processMIDIByte(byte);
    }
#endif
}

bool SerialMIDIHandler::isConnected() {
    return connected;
}

void SerialMIDIHandler::onControlChange(void (*callback)(uint8_t, uint8_t, uint8_t)) {
    ccCallback = callback;
}

void SerialMIDIHandler::onNoteOn(void (*callback)(uint8_t, uint8_t, uint8_t)) {
    noteOnCallback = callback;
}

void SerialMIDIHandler::onNoteOff(void (*callback)(uint8_t, uint8_t, uint8_t)) {
    noteOffCallback = callback;
}

void SerialMIDIHandler::processMIDIByte(uint8_t byte) {
    // Status byte (MSB = 1)
    if (byte & 0x80) {
        // Real-time messages (0xF8-0xFF) - ignore for now
        if (byte >= 0xF8) {
            return;
        }
        
        // System messages (0xF0-0xF7) - ignore for now
        if (byte >= 0xF0) {
            bufferIndex = 0;
            expectedBytes = 0;
            return;
        }
        
        // Channel voice message
        runningStatus = byte;
        midiBuffer[0] = byte;
        bufferIndex = 1;
        expectedBytes = getMessageLength(byte);
        return;
    }
    
    // Data byte (MSB = 0)
    if (expectedBytes > 0) {
        midiBuffer[bufferIndex++] = byte;
        
        if (bufferIndex >= expectedBytes) {
            processCompleteMessage();
            // Reset for next message (running status still active)
            bufferIndex = 1;
            midiBuffer[0] = runningStatus;
        }
    }
}

void SerialMIDIHandler::processCompleteMessage() {
    lastMessageTime = millis();
    connected = true;
    
    uint8_t status = midiBuffer[0] & 0xF0;
    uint8_t channel = (midiBuffer[0] & 0x0F) + 1; // Convert to 1-based
    
    switch (status) {
        case 0x80: // Note Off
            _processor.handleNoteOff(channel, midiBuffer[1], midiBuffer[2]);
            if (noteOffCallback && channel == MIDI_CHANNEL) {
                noteOffCallback(channel, midiBuffer[1], midiBuffer[2]);
            }
            break;
            
        case 0x90: // Note On
            if (midiBuffer[2] == 0) {
                _processor.handleNoteOff(channel, midiBuffer[1], 0);
                if (noteOffCallback && channel == MIDI_CHANNEL) {
                    noteOffCallback(channel, midiBuffer[1], 0);
                }
            } else {
                _processor.handleNoteOn(channel, midiBuffer[1], midiBuffer[2]);
                if (noteOnCallback && channel == MIDI_CHANNEL) {
                    noteOnCallback(channel, midiBuffer[1], midiBuffer[2]);
                }
            }
            break;
            
        case 0xB0: // Control Change
            _processor.handleControlChange(channel, midiBuffer[1], midiBuffer[2]);
            if (ccCallback) {
                ccCallback(channel, midiBuffer[1], midiBuffer[2]);
            }
            break;
            
        // Add other message types as needed
        default:
            break;
    }
}

uint8_t SerialMIDIHandler::getMessageLength(uint8_t status) {
    status = status & 0xF0;
    
    switch (status) {
        case 0x80: // Note Off
        case 0x90: // Note On
        case 0xA0: // Polyphonic Aftertouch
        case 0xB0: // Control Change
        case 0xE0: // Pitch Bend
            return 3;
            
        case 0xC0: // Program Change
        case 0xD0: // Channel Aftertouch
            return 2;
            
        default:
            return 0;
    }
}
