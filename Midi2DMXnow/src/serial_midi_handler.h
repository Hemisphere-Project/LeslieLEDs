#ifndef SERIAL_MIDI_HANDLER_H
#define SERIAL_MIDI_HANDLER_H

#include <Arduino.h>
#include "config.h"

// Forward declarations
class DMXState;
class DisplayHandler;

/**
 * Serial MIDI Handler for ESP32 devices without native USB MIDI
 * Implements raw MIDI-over-Serial communication at 115200 baud
 * Compatible with controller's MIDI-to-Serial bridge mode
 */
class SerialMIDIHandler {
public:
    SerialMIDIHandler();
    
    /**
     * Initialize Serial MIDI communication
     */
    void begin();
    
    /**
     * Process incoming MIDI messages from Serial
     * Call this regularly in the main loop
     */
    void update();
    
    /**
     * Check if connected and receiving data
     */
    bool isConnected();
    
    /**
     * Set DMX State reference (for MIDI control)
     */
    void setDMXState(DMXState* state);

    /**
     * Set display handler for logging and notifications
     */
    void setDisplayHandler(DisplayHandler* display);
    
    /**
     * Set callback for Control Change messages
     * @param callback Function to call when CC message received
     */
    void onControlChange(void (*callback)(uint8_t channel, uint8_t cc, uint8_t value));
    
    /**
     * Set callback for Note On messages
     * @param callback Function to call when Note On received
     */
    void onNoteOn(void (*callback)(uint8_t channel, uint8_t note, uint8_t velocity));
    
    /**
     * Set callback for Note Off messages
     * @param callback Function to call when Note Off received
     */
    void onNoteOff(void (*callback)(uint8_t channel, uint8_t note, uint8_t velocity));

private:
    // Component references
    DMXState* dmxState;
    DisplayHandler* displayHandler;
    
    // MIDI message parsing state
    uint8_t midiBuffer[3];
    uint8_t bufferIndex;
    uint8_t expectedBytes;
    uint8_t runningStatus;
    
    // Callbacks
    void (*ccCallback)(uint8_t channel, uint8_t cc, uint8_t value);
    void (*noteOnCallback)(uint8_t channel, uint8_t note, uint8_t velocity);
    void (*noteOffCallback)(uint8_t channel, uint8_t note, uint8_t velocity);
    
    // Connection tracking
    unsigned long lastMessageTime;
    bool connected;
    
    // Helper methods
    void processMIDIByte(uint8_t byte);
    void processCompleteMessage();
    uint8_t getMessageLength(uint8_t status);
    void handleControlChange(uint8_t channel, uint8_t cc, uint8_t value);
    void handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);
};

#endif // SERIAL_MIDI_HANDLER_H
