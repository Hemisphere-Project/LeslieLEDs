#include "boot_beacon.h"

#include <libstrip.h>

bool BootBeacon::begin(uint8_t gpio) {
    LibStrip::init();

    strand_t s = {};
    s.rmtChannel = 1;
    s.gpioNum = gpio;
    s.ledType = LED_SK6812_V1;
    s.brightLimit = 255;
    s.numPixels = 1;

    _strand = LibStrip::addStrand(s);
    _state = BOOT;
    _lastBlink = millis();
    _lastActivityPulse = 0;
    _activityPulseUntil = 0;
    _blinkOn = true;
    return _strand != nullptr;
}

void BootBeacon::setState(State s) {
    _state = s;
    _lastBlink = millis();
    _lastActivityPulse = 0;
    _activityPulseUntil = 0;
    _blinkOn = true;
    switch (s) {
        case BOOT:     writeColor(80, 0, 0);   break;  // dim red
        case READY:    writeColor(0, 60, 0);   break;  // dim green
        case LOST:     writeColor(200, 0, 0);  break;  // bright red, blinker takes over
        case BROWNOUT: writeColor(120, 0, 120); break; // purple, blinker takes over
        case OFF:      writeColor(0, 0, 0);    break;
    }
}

void BootBeacon::noteDMXActivity(uint32_t now) {
    if (_state != READY) return;

    // Human-visible receive indication: pulse to a brighter green at most
    // ~4 times per second while DMX is flowing, leaving READY as dim green.
    if (_activityPulseUntil > now) return;
    if (_lastActivityPulse > 0 && (now - _lastActivityPulse) < 250) return;

    _lastActivityPulse = now;
    _activityPulseUntil = now + 90;
    writeColor(0, 180, 0);
}

void BootBeacon::tick(uint32_t now) {
    switch (_state) {
        case READY:
            if (_activityPulseUntil > 0 && now >= _activityPulseUntil) {
                _activityPulseUntil = 0;
                writeColor(0, 60, 0);
            }
            break;
        case LOST:
            if (now - _lastBlink < 500) return;
            _blinkOn = !_blinkOn;
            _lastBlink = now;
            writeColor(_blinkOn ? 200 : 0, 0, 0);
            break;
        case BROWNOUT:
            // Slow purple breathing so it's distinguishable from LOST.
            if (now - _lastBlink < 800) return;
            _blinkOn = !_blinkOn;
            _lastBlink = now;
            writeColor(_blinkOn ? 120 : 20, 0, _blinkOn ? 120 : 20);
            break;
        default:
            break;
    }
}

void BootBeacon::writeColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!_strand) return;
    auto* st = reinterpret_cast<strand_t*>(_strand);
    if (!st->pixels) return;
    st->pixels[0].r = r;
    st->pixels[0].g = g;
    st->pixels[0].b = b;
    st->pixels[0].w = 0;
    LibStrip::updatePixels(st);
}
