#ifndef DMX_OUTPUT_H
#define DMX_OUTPUT_H

#include <Arduino.h>

/**
 * PhysicalDMXOutput - Outputs RGBW values to a physical DMX512 interface
 * 
 * This class takes a DMX frame (from ESP-NOW DMX state) and outputs 
 * the primary color (colorA) as RGBW on DMX channels 1-4, with strobe applied.
 * 
 * Usage is similar to LedEngine: create instance, call begin(), then update() each frame.
 */
class PhysicalDMXOutput {
public:
    struct Config {
        uint8_t txPin;
        uint8_t rxPin;
        uint8_t enablePin;
        uint8_t dmxPort;
        uint32_t refreshIntervalMs;
        
        Config() 
            : txPin(5)              // DMX TX pin (AtomS3 PortC = GPIO5)
            , rxPin(6)              // DMX RX pin (not used for output)
            , enablePin(255)        // Optional RS485 enable pin (255 = not used)
            , dmxPort(1)            // DMX port number (1 or 2 on ESP32)
            , refreshIntervalMs(33) // ~30Hz refresh rate
        {}
    };

    PhysicalDMXOutput();
    ~PhysicalDMXOutput();

    /**
     * Initialize the DMX output hardware
     * @param config Configuration for pins and timing
     * @return true if initialization successful
     */
    bool begin(const Config& config = Config());

    /**
     * Update the DMX output - call this every frame
     * @param dmxFrame The full DMX frame (512 bytes) containing state
     * @param meshMillis Synchronized mesh time for strobe calculation
     */
    void update(const uint8_t* dmxFrame, unsigned long meshMillis);

    /**
     * Check if DMX output is initialized and running
     */
    bool isRunning() const { return _running; }

private:
    Config _config;
    bool _running;
    unsigned long _lastSendTime;
    uint8_t _outputBuffer[5];  // Channels 1-4 (RGBW) + start code

    // HSV to RGB conversion
    void hsvToRgb(uint8_t h, uint8_t s, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b);
    
    // Apply strobe effect
    uint8_t applyStrobe(uint8_t value, uint8_t strobeRate, unsigned long meshMillis);
    
    // Send DMX frame to hardware
    void sendDMXFrame();
};

#endif // DMX_OUTPUT_H
