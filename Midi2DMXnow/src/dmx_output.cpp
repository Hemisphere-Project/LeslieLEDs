#include "dmx_output.h"
#include "config.h"
#include <driver/uart.h>
#include <driver/gpio.h>

// DMX512 timing constants
#define DMX_BREAK_US 176        // Break: minimum 92µs, typically 176µs
#define DMX_MAB_US 12           // Mark After Break: minimum 12µs
#define DMX_BAUD 250000         // DMX baud rate: 250kbps

PhysicalDMXOutput::PhysicalDMXOutput()
    : _running(false)
    , _lastSendTime(0)
{
    memset(_outputBuffer, 0, sizeof(_outputBuffer));
}

PhysicalDMXOutput::~PhysicalDMXOutput() {
    if (_running) {
        uart_driver_delete((uart_port_t)_config.dmxPort);
        _running = false;
    }
}

bool PhysicalDMXOutput::begin(const Config& config) {
    _config = config;
    
    uart_port_t uartNum = (uart_port_t)_config.dmxPort;
    
    // Configure UART for DMX (8N2 at 250kbaud)
    uart_config_t uartConfig = {
        .baud_rate = DMX_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    // Install UART driver
    esp_err_t err = uart_driver_install(uartNum, 256, 256, 0, NULL, 0);
    if (err != ESP_OK) {
        #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
        Serial.printf("[ERR] Failed to install UART driver: %d\n", err);
        #endif
        return false;
    }
    
    err = uart_param_config(uartNum, &uartConfig);
    if (err != ESP_OK) {
        #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
        Serial.printf("[ERR] Failed to configure UART: %d\n", err);
        #endif
        uart_driver_delete(uartNum);
        return false;
    }
    
    // Set pins (TX only for DMX output)
    err = uart_set_pin(uartNum, _config.txPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
        Serial.printf("[ERR] Failed to set UART pins: %d\n", err);
        #endif
        uart_driver_delete(uartNum);
        return false;
    }
    
    _running = true;
    
    #if DEBUG_MODE && !defined(USE_SERIAL_MIDI)
    Serial.printf("[DMX] Physical output initialized on GPIO%d (30Hz)\n", _config.txPin);
    #endif
    
    return true;
}

void PhysicalDMXOutput::hsvToRgb(uint8_t h, uint8_t s, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (s == 0) {
        r = g = b = v;
        return;
    }

    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;

    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

uint8_t PhysicalDMXOutput::applyStrobe(uint8_t value, uint8_t strobeRate, unsigned long meshMillis) {
    if (strobeRate == 0) {
        return value;  // No strobe
    }
    
    // Map strobeRate (0-255) to frequency: higher value = faster strobe
    // Range: ~1Hz (strobeRate=1) to ~25Hz (strobeRate=255)
    uint32_t periodMs = 1000 - (strobeRate * 3);  // ~1000ms down to ~235ms period
    if (periodMs < 40) periodMs = 40;  // Cap at 25Hz
    
    // 50% duty cycle square wave
    uint32_t phase = meshMillis % periodMs;
    bool strobeOn = (phase < (periodMs / 2));
    
    return strobeOn ? value : 0;
}

void PhysicalDMXOutput::update(const uint8_t* dmxFrame, unsigned long meshMillis) {
    if (!_running || dmxFrame == nullptr) {
        return;
    }
    
    // Rate limit to configured refresh interval
    unsigned long now = millis();
    if (now - _lastSendTime < _config.refreshIntervalMs) {
        return;
    }
    _lastSendTime = now;
    
    // Extract values from DMX frame
    uint8_t masterBrightness = dmxFrame[DMX_CH_MASTER_BRIGHTNESS];
    uint8_t strobeRate = dmxFrame[DMX_CH_STROBE_RATE];
    
    // ColorA HSV values
    uint8_t hue = dmxFrame[DMX_CH_COLOR_A_HUE];
    uint8_t sat = dmxFrame[DMX_CH_COLOR_A_SATURATION];
    uint8_t val = dmxFrame[DMX_CH_COLOR_A_VALUE];
    uint8_t white = dmxFrame[DMX_CH_COLOR_A_WHITE];
    
    // Convert HSV to RGB
    uint8_t r, g, b;
    hsvToRgb(hue, sat, val, r, g, b);
    
    // Apply master brightness
    r = (r * masterBrightness) / 255;
    g = (g * masterBrightness) / 255;
    b = (b * masterBrightness) / 255;
    white = (white * masterBrightness) / 255;
    
    // Apply strobe to all channels
    r = applyStrobe(r, strobeRate, meshMillis);
    g = applyStrobe(g, strobeRate, meshMillis);
    b = applyStrobe(b, strobeRate, meshMillis);
    white = applyStrobe(white, strobeRate, meshMillis);
    
    // Prepare output buffer (DMX channels 1-4 = RGBW)
    // Note: DMX channel 0 is the start code, channels 1-512 are data
    _outputBuffer[0] = 0;      // Start code
    _outputBuffer[1] = r;      // Channel 1: Red
    _outputBuffer[2] = g;      // Channel 2: Green
    _outputBuffer[3] = b;      // Channel 3: Blue
    _outputBuffer[4] = white;  // Channel 4: White
    
    // Send DMX frame
    sendDMXFrame();
}

void PhysicalDMXOutput::sendDMXFrame() {
    uart_port_t uartNum = (uart_port_t)_config.dmxPort;
    
    // Wait for any previous transmission to complete
    uart_wait_tx_done(uartNum, pdMS_TO_TICKS(10));
    
    // Send DMX Break (pull line low for 176µs)
    // We do this by temporarily setting break length and sending break
    uart_set_line_inverse(uartNum, UART_SIGNAL_TXD_INV);
    delayMicroseconds(DMX_BREAK_US);
    uart_set_line_inverse(uartNum, 0);
    
    // Mark After Break (MAB) - line high for 12µs (happens naturally with idle)
    delayMicroseconds(DMX_MAB_US);
    
    // Send start code + RGBW data (5 bytes total)
    uart_write_bytes(uartNum, (const char*)_outputBuffer, sizeof(_outputBuffer));
}
