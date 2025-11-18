#ifndef MIDI_HANDLER_H
#define MIDI_HANDLER_H

#include <Arduino.h>
#include <USBMIDI.h>

// Forward declarations
class LEDController;
class DisplayHandler;

class MIDIHandler {
public:
    MIDIHandler();
    
    void begin();
    void update();
    
    // Set references to other components
    void setLEDController(LEDController* controller);
    void setDisplayHandler(DisplayHandler* display);
    
    // Get last received message info for display
    const char* getLastMessage() const { return _lastMessage; }
    unsigned long getLastMessageTime() const { return _lastMessageTime; }

private:
    USBMIDI _midi;
    LEDController* _ledController;
    DisplayHandler* _displayHandler;
    
    char _lastMessage[32];
    unsigned long _lastMessageTime;
    
    void handleControlChange(byte channel, byte controller, byte value);
    void handleNoteOn(byte channel, byte note, byte velocity);
    void handleNoteOff(byte channel, byte note, byte velocity);
    
    void updateLastMessage(const char* msg);
};

#endif // MIDI_HANDLER_H
