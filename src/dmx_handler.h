#ifndef DMX_HANDLER_H
#define DMX_HANDLER_H

#include <Arduino.h>
#include "config.h"

// LXESP32DMX library for both platforms
#include <LXESP32DMX.h>

// Forward declaration
class LEDController;

/**
 * DMX Handler
 * Manages DMX512 output for multiple external lighting fixtures
 * 
 * Hardware:
 * - AtomS3: M5Unit-DMX512 on PortC (GPIO5/6) via UART
 * - M5Core: M5Module-DMX512 on Serial (TX=13, RX=35, EN=12) using LXESP32DMX
 * 
 * Multi-Fixture Mapping:
 * - Each fixture uses 31 DMX channels
 * - Fixture N starts at channel: ((N-1) * 31) + 1
 * - Fixture 1: Channels 1-31, Fixture 2: Channels 32-62, etc.
 * - Maximum 16 fixtures supported (496 channels)
 * 
 * Channel Layout per Fixture (first 6 channels of 31):
 * Ch0 (+0): Red (0-255)
 * Ch1 (+1): Green (0-255)
 * Ch2 (+2): Blue (0-255)
 * Ch3 (+3): White (0-255)
 * Ch4 (+4): Master/Dimmer (0-255)
 * Ch5-30 (+5-30): Unused/Reserved for future expansion
 * 
 * Pixel Mapping:
 * - Each fixture maps to a specific LED strip pixel
 * - Configurable via g_dmxPixelMap[] array
 * - Default: Fixture 1 = Pixel 0, Fixture 2 = Pixel 10, etc.
 */
class DMXHandler {
public:
    DMXHandler();
    
    void begin();
    void update();
    
    // Set LED controller reference for reading state
    void setLEDController(LEDController* controller);
    
    // Manual DMX channel control (if needed)
    void setChannel(uint16_t channel, uint8_t value);
    void sendDMX();
    
private:
    LEDController* _ledController;
    
    uint8_t _dmxBuffer[DMX_CHANNELS];
    unsigned long _lastUpdate;
    bool _strobeState;
    
    // DMX fixture control
    void updateFixture();
    void setRGBW(uint16_t startChannel, uint8_t r, uint8_t g, uint8_t b, uint8_t w);
    void setMaster(uint16_t channel, uint8_t value);
};

#endif // DMX_HANDLER_H
