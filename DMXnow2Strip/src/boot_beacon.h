#ifndef BOOT_BEACON_H
#define BOOT_BEACON_H

#include <Arduino.h>

// Tiny driver for the Atom Lite onboard SK6812 LED.
// Used as a visual "this node is alive / connected / lost" indicator so
// the rig operator can tell which receiver is in which state without USB
// or a screen. Allocates its own RMT channel via libstrip; does not
// interfere with the main LED strip.
class BootBeacon {
public:
    enum State {
        BOOT,      // dim red solid — chip is awake, setup() still running
        READY,     // dim green solid — DMX link healthy
        LOST,      // red slow blink — no DMX frame received for a while
        BROWNOUT,  // magenta/purple blink — last reset was a brownout; check power
        OFF
    };

    bool begin(uint8_t gpio);
    void setState(State s);
    void tick(uint32_t now);

private:
    void writeColor(uint8_t r, uint8_t g, uint8_t b);

    void* _strand;
    State _state;
    uint32_t _lastBlink;
    bool _blinkOn;
};

#endif // BOOT_BEACON_H
