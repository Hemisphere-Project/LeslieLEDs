#include "dmx_handler.h"
#include "led_controller.h"

// Global pixel mapping array: defines which LED pixel each fixture controls
// Default mapping: Fixture 1 = Pixel 0, Fixture 2 = Pixel 10, Fixture 3 = Pixel 20, etc.
// This can be dynamically updated via MIDI or other control methods
uint16_t g_dmxPixelMap[DMX_MAX_FIXTURES] = {
    0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150
};

// Number of active fixtures to update (1-16)
uint8_t g_dmxActiveFixtures = 4;  // Start with 4 fixtures by default

DMXHandler::DMXHandler()
    : _ledController(nullptr)
    , _lastUpdate(0)
    , _strobeState(false)
{
    // Initialize DMX buffer to all zeros
    memset(_dmxBuffer, 0, sizeof(_dmxBuffer));
}

void DMXHandler::begin() {
    #if defined(PLATFORM_M5CORE) && defined(DMX_EN_PIN)
        // M5Module-DMX512 requires enable pin HIGH for RS485 transceiver
        pinMode(DMX_EN_PIN, OUTPUT);
        digitalWrite(DMX_EN_PIN, HIGH);
        #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
            Serial.printf("DMX Enable pin (GPIO%d) set HIGH\n", DMX_EN_PIN);
        #endif
        delay(10);  // Give transceiver time to enable
    #endif
    
    // Initialize LXESP32DMX - same for both platforms
    ESP32DMX.startOutput(DMX_TX_PIN);
    
    #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
        #if defined(PLATFORM_ATOMS3)
            Serial.printf("DMX initialized: LXESP32DMX on PortC (TX=%d)\n", DMX_TX_PIN);
        #elif defined(PLATFORM_M5CORE)
            Serial.printf("DMX initialized: LXESP32DMX on M5Module-DMX512 (TX=%d, EN=%d)\n", DMX_TX_PIN, DMX_EN_PIN);
        #endif
        Serial.printf("DMX channels: %d\n", DMX_CHANNELS);
        Serial.printf("Multi-fixture mode: %d fixtures @ %d channels each\n", g_dmxActiveFixtures, DMX_CHANNELS_PER_FIXTURE);
        Serial.println("Pixel mapping:");
        for (uint8_t i = 0; i < g_dmxActiveFixtures; i++) {
            uint16_t dmxStart = (i * DMX_CHANNELS_PER_FIXTURE) + DMX_FIXTURE_START;
            Serial.printf("  Fixture %d: Pixel %d -> DMX channels %d-%d\n", 
                i + 1, g_dmxPixelMap[i], dmxStart, dmxStart + DMX_CHANNELS_PER_FIXTURE - 1);
        }
    #endif
    
    // Channels are initialized to 0 by startOutput()
}

void DMXHandler::update() {
    if (!_ledController) return;
    
    // Update DMX at configured rate
    unsigned long now = millis();
    if (now - _lastUpdate >= DMX_UPDATE_INTERVAL_MS) {
        updateFixture();
        sendDMX();
        _lastUpdate = now;
    }
}

void DMXHandler::updateFixture() {
    if (!_ledController) return;
    
    // Get current LED state
    uint8_t masterBrightness = _ledController->getMasterBrightness();
    uint8_t strobeRate = _ledController->getStrobeRate();
    
    // Handle strobe effect
    bool outputEnabled = true;
    if (strobeRate > 0) {
        // Strobe frequency increases with rate (0=off, 127=fastest)
        // Generate strobe by toggling output on/off
        unsigned long strobeInterval = map(strobeRate, 1, 127, 500, 20); // 500ms to 20ms
        unsigned long now = millis();
        _strobeState = (now / strobeInterval) % 2 == 0;
        outputEnabled = _strobeState;
    }
    
    // Update each active fixture
    for (uint8_t fixtureNum = 0; fixtureNum < g_dmxActiveFixtures; fixtureNum++) {
        // Get pixel index for this fixture
        uint16_t pixelIndex = g_dmxPixelMap[fixtureNum];
        
        // Calculate DMX start channel for this fixture
        // Fixture 1 (index 0) -> Channel 1, Fixture 2 (index 1) -> Channel 32, etc.
        uint16_t dmxStart = (fixtureNum * DMX_CHANNELS_PER_FIXTURE) + DMX_FIXTURE_START;
        
        // Get color for this pixel (using colorA for all pixels currently)
        // In the future, this could read individual pixel colors from LED strip
        const ColorRGBW& color = _ledController->getColorA();
        
        if (outputEnabled) {
            // Scale RGB by master brightness
            uint8_t r = map(color.r * masterBrightness, 0, 255*255, 0, 255);
            uint8_t g = map(color.g * masterBrightness, 0, 255*255, 0, 255);
            uint8_t b = map(color.b * masterBrightness, 0, 255*255, 0, 255);
            uint8_t w = map(color.w * masterBrightness, 0, 255*255, 0, 255);
            
            // Set RGBW channels (0-3 of the 31-channel fixture)
            setRGBW(dmxStart, r, g, b, w);
            // Set master dimmer (channel 4 of the 31-channel fixture)
            setMaster(dmxStart + 4, masterBrightness);
        } else {
            // Strobe off state - set all to 0
            setRGBW(dmxStart, 0, 0, 0, 0);
            setMaster(dmxStart + 4, 0);
        }
        
        // Channels 5-30 of each fixture are unused/reserved - remain at 0
    }
}

void DMXHandler::setRGBW(uint16_t startChannel, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (startChannel + 3 >= DMX_CHANNELS) return;
    
    // DMX channels are 1-indexed, buffer is 0-indexed
    _dmxBuffer[startChannel - 1] = r;     // Channel 1: Red
    _dmxBuffer[startChannel] = g;         // Channel 2: Green
    _dmxBuffer[startChannel + 1] = b;     // Channel 3: Blue
    _dmxBuffer[startChannel + 2] = w;     // Channel 4: White
}

void DMXHandler::setMaster(uint16_t channel, uint8_t value) {
    if (channel >= DMX_CHANNELS) return;
    _dmxBuffer[channel - 1] = value;      // Channel is 1-indexed
}

void DMXHandler::setChannel(uint16_t channel, uint8_t value) {
    if (channel >= DMX_CHANNELS || channel == 0) return;
    _dmxBuffer[channel - 1] = value;      // Convert to 0-indexed
}

void DMXHandler::sendDMX() {
    // LXESP32DMX: Update DMX slots from buffer
    // Using semaphore for thread safety
    if (xSemaphoreTake(ESP32DMX.lxDataLock, portMAX_DELAY) == pdTRUE) {
        for(int i = 0; i < DMX_CHANNELS; i++) {
            ESP32DMX.setSlot(i+1, _dmxBuffer[i]);  // setSlot is 1-indexed
        }
        xSemaphoreGive(ESP32DMX.lxDataLock);
    }
}

void DMXHandler::setLEDController(LEDController* controller) {
    _ledController = controller;
}
