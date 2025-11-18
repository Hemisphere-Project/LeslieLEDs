#include "serial_midi_handler.h"
#include "dmx_state.h"
#include "display_handler.h"
#include <M5Unified.h>

#define CONNECTION_TIMEOUT_MS 5000

SerialMIDIHandler::SerialMIDIHandler() :
    dmxState(nullptr),
    displayHandler(nullptr),
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
    dmxState = state;
}

void SerialMIDIHandler::setDisplayHandler(DisplayHandler* display) {
    displayHandler = display;
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
            handleNoteOff(channel, midiBuffer[1], midiBuffer[2]);
            break;
            
        case 0x90: // Note On
            // Note On with velocity 0 is Note Off
            if (midiBuffer[2] == 0) {
                handleNoteOff(channel, midiBuffer[1], 0);
            } else {
                handleNoteOn(channel, midiBuffer[1], midiBuffer[2]);
            }
            break;
            
        case 0xB0: // Control Change
            handleControlChange(channel, midiBuffer[1], midiBuffer[2]);
            break;
            
        // Add other message types as needed
        default:
            break;
    }
}

void SerialMIDIHandler::handleControlChange(uint8_t channel, uint8_t cc, uint8_t value) {
    if (!dmxState) return;
    
    // Route CCs to appropriate handlers
    if (cc >= 1 && cc <= 19) {
        // Global controls
        dmxState->handleGlobalCC(cc, value);
    } else if (cc >= 20 && cc <= 29) {
        // Color A controls
        dmxState->handleColorCC(0, cc, value);
    } else if (cc >= 30 && cc <= 39) {
        // Color B controls
        dmxState->handleColorCC(1, cc, value);
    } else if (cc >= 40 && cc <= 127) {
        // Other controls (effects, system, etc.)
        dmxState->handleGlobalCC(cc, value);
    }
    
    // Also call registered callback if any (for display logging)
    if (ccCallback) {
        ccCallback(channel, cc, value);
    }

    if (displayHandler) {
        char msg[32];
        snprintf(msg, sizeof(msg), "CC%d=%d", cc, value);
        displayHandler->logMessage(msg);
    }
}

void SerialMIDIHandler::handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (channel != MIDI_CHANNEL) {
        return;
    }

    DMXState::SceneEvent event = {};
    if (dmxState) {
        event = dmxState->handleNoteOn(note, velocity);
    }
    
    // Also call registered callback if any
    if (noteOnCallback) {
        noteOnCallback(channel, note, velocity);
    }

    if (displayHandler) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Note %d ON", note);
        displayHandler->logMessage(msg);

        if (event.triggered) {
            if (event.blackout) {
                displayHandler->logMessage("Blackout");
            } else {
                displayHandler->showSceneNotification(event.sceneIndex, event.saved);
            }
        }
    }
}

void SerialMIDIHandler::handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (channel != MIDI_CHANNEL) {
        return;
    }

    if (dmxState) {
        dmxState->handleNoteOff(note);
    }
    
    // Also call registered callback if any
    if (noteOffCallback) {
        noteOffCallback(channel, note, velocity);
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
