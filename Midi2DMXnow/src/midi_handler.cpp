#include "midi_handler.h"
#include "dmx_state.h"
#include "display_handler.h"
#include "heartbeat_collector.h"
#include "config.h"
#include <USB.h>

namespace {

constexpr uint8_t SCENE_BANK_STATUS_OK = 0;
constexpr uint8_t SCENE_BANK_STATUS_BAD_VERSION = 1;
constexpr uint8_t SCENE_BANK_STATUS_BAD_SIZE = 2;
constexpr uint8_t SCENE_BANK_STATUS_DECODE_ERROR = 3;
constexpr uint8_t SCENE_BANK_STATUS_APPLY_ERROR = 4;

size_t decodeNibbleBytes(const uint8_t* encoded,
                        size_t encodedSize,
                        uint8_t* out,
                        size_t outCapacity) {
    if (!encoded || !out || (encodedSize % 2) != 0) {
        return 0;
    }

    size_t rawSize = encodedSize / 2;
    if (rawSize > outCapacity) {
        return 0;
    }

    for (size_t i = 0; i < rawSize; ++i) {
        uint8_t hi = encoded[i * 2];
        uint8_t lo = encoded[i * 2 + 1];
        if (hi > 0x0F || lo > 0x0F) {
            return 0;
        }
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }

    return rawSize;
}

}

MIDIHandler::MIDIHandler()
    : _dmxState(nullptr), _heartbeats(nullptr),
      _lastSysExSend(0), _lastRigHealthSend(0),
      _sysexRxLength(0), _sysexRxActive(false) {}

void MIDIHandler::begin() {
    // Set USB device name before starting
    USB.productName(MIDI_DEVICE_NAME);
    USB.manufacturerName("LeslieLEDs");
    
    _midi.begin();
    USB.begin();
    _processor.postStatusMessage("MIDI Ready");
}

void MIDIHandler::setDMXState(DMXState* state) {
    _dmxState = state;
    _processor.setDMXState(state);
}

void MIDIHandler::setDisplayHandler(DisplayHandler* display) {
    _processor.setDisplayHandler(display);
}

void MIDIHandler::setHeartbeats(HeartbeatCollector* hb) {
    _heartbeats = hb;
}

void MIDIHandler::sendStateSysEx() {
    if (!_dmxState) return;
    
    // Build SysEx message: F0 7D 01 <17 bytes of data> F7
    // Total 20 bytes
    uint8_t sysex[20];
    sysex[0] = 0xF0;  // SysEx start
    sysex[1] = SYSEX_MANUFACTURER_ID;
    sysex[2] = SYSEX_MSG_STATE_DUMP;
    
    // Pack state data (values must be 0-127 for MIDI, scale 255->127)
    sysex[3] = _dmxState->getMasterBrightness() >> 1;  // CC 1
    sysex[4] = _dmxState->getAnimationSpeed() >> 1;    // CC 2
    sysex[5] = _dmxState->getAnimationCtrl() >> 1;     // CC 3
    sysex[6] = _dmxState->getStrobeRate() >> 1;        // CC 4
    sysex[7] = _dmxState->getBlendMode() >> 1;         // CC 5
    sysex[8] = _dmxState->getMirror() >> 1;            // CC 6
    sysex[9] = _dmxState->getDirection() >> 1;         // CC 7
    sysex[10] = static_cast<uint8_t>(_dmxState->getCurrentMode());  // CC 8 (already 0-9)
    
    const HSVColor& colorA = _dmxState->getColorA();
    sysex[11] = colorA.hue >> 1;         // CC 20
    sysex[12] = colorA.saturation >> 1;  // CC 21
    sysex[13] = colorA.value >> 1;       // CC 22
    sysex[14] = colorA.white >> 1;       // CC 23
    
    const HSVColor& colorB = _dmxState->getColorB();
    sysex[15] = colorB.hue >> 1;         // CC 30
    sysex[16] = colorB.saturation >> 1;  // CC 31
    sysex[17] = colorB.value >> 1;       // CC 32
    sysex[18] = colorB.white >> 1;       // CC 33
    
    // Active scene (0-19, or 127 for none)
    int8_t scene = _dmxState->getCurrentScene();
    sysex[19] = (scene >= 0 && scene < MAX_SCENES) ? scene : 127;
    
    // Note: SysEx end (F7) is handled by sending byte-by-byte
    // The USBMIDI write() method handles SysEx parsing
    for (int i = 0; i < 20; i++) {
        _midi.write(sysex[i]);
    }
    _midi.write(0xF7);  // SysEx end
}

void MIDIHandler::sendRigHealthSysEx() {
    if (!_heartbeats) return;

    // Per-slot payload: nid[0..2] (masked to 7 bits), status (0-4),
    // fps (0-127), resetReason (0-127).  6 bytes × 8 slots max = 48 bytes.
    // Full message: F0 7D 02 <count> [slots...] F7
    HeartbeatCollector::Slot slots[HeartbeatCollector::MAX_SLAVES];
    _heartbeats->copySlots(slots);  // thread-safe snapshot
    const uint32_t now = millis();

    auto statusFromSnapshot = [now](const HeartbeatCollector::Slot& slot) {
        if (!slot.used) return HeartbeatCollector::EMPTY;

        const uint32_t age = now - slot.lastHeardLocalMs;
        if (age >= HeartbeatCollector::LOST_MS) return HeartbeatCollector::LOST;
        if (age >= HeartbeatCollector::STALE_MS) return HeartbeatCollector::STALE;
        if (slot.last.msSinceLastFrame >= HeartbeatCollector::NO_DMX_FRAME_MS) {
            return HeartbeatCollector::NO_DMX;
        }
        return HeartbeatCollector::OK;
    };

    uint8_t buf[4 + HeartbeatCollector::MAX_SLAVES * 6 + 1];
    buf[0] = 0xF0;
    buf[1] = SYSEX_MANUFACTURER_ID;
    buf[2] = SYSEX_MSG_RIG_HEALTH;

    uint8_t count = 0;
    uint8_t* p = buf + 4;  // reserve byte 3 for count
    for (uint8_t i = 0; i < HeartbeatCollector::MAX_SLAVES; ++i) {
        const auto& s = slots[i];
        if (!s.used) continue;
        p[0] = s.nid[0] & 0x7F;
        p[1] = s.nid[1] & 0x7F;
        p[2] = s.nid[2] & 0x7F;
        p[3] = static_cast<uint8_t>(statusFromSnapshot(s));
        p[4] = s.last.fps & 0x7F;
        p[5] = s.last.lastResetReason & 0x7F;
        p += 6;
        ++count;
    }
    buf[3] = count;
    *p++ = 0xF7;

    const int totalLen = static_cast<int>(p - buf);
    for (int i = 0; i < totalLen; ++i) {
        _midi.write(buf[i]);
    }
}

void MIDIHandler::resetSysExReceive() {
    _sysexRxLength = 0;
    _sysexRxActive = false;
}

void MIDIHandler::processSysExPacket(const midiEventPacket_t& packet, uint8_t cin) {
    uint8_t bytes[3] = {packet.byte1, packet.byte2, packet.byte3};

    switch (cin) {
        case 0x04:
            appendSysExBytes(bytes, 3, false);
            break;
        case 0x05:
            appendSysExBytes(bytes, 1, true);
            break;
        case 0x06:
            appendSysExBytes(bytes, 2, true);
            break;
        case 0x07:
            appendSysExBytes(bytes, 3, true);
            break;
        default:
            break;
    }
}

void MIDIHandler::appendSysExBytes(const uint8_t* data, size_t count, bool complete) {
    for (size_t i = 0; i < count; ++i) {
        uint8_t value = data[i];
        if (!_sysexRxActive) {
            if (value != 0xF0) {
                continue;
            }
            _sysexRxActive = true;
            _sysexRxLength = 0;
        }

        if (_sysexRxLength >= SYSEX_RX_MAX_BYTES) {
            resetSysExReceive();
            return;
        }

        _sysexRxBuffer[_sysexRxLength++] = value;
    }

    if (complete && _sysexRxActive) {
        handleCompletedSysEx();
        resetSysExReceive();
    }
}

void MIDIHandler::handleCompletedSysEx() {
    if (_sysexRxLength < 5) {
        return;
    }
    if (_sysexRxBuffer[0] != 0xF0 || _sysexRxBuffer[_sysexRxLength - 1] != 0xF7) {
        return;
    }
    if (_sysexRxBuffer[1] != SYSEX_MANUFACTURER_ID) {
        return;
    }

    uint8_t msgType = _sysexRxBuffer[2];
    if (msgType == SYSEX_MSG_SCENE_BANK_REQUEST) {
        if (_sysexRxLength != 5) {
            sendSceneBankStatusSysEx(SCENE_BANK_STATUS_BAD_SIZE, 0);
            return;
        }
        if (_sysexRxBuffer[3] != DMXState::SCENE_BANK_WIRE_VERSION) {
            sendSceneBankStatusSysEx(SCENE_BANK_STATUS_BAD_VERSION, _sysexRxBuffer[3]);
            return;
        }
        sendSceneBankDumpSysEx();
        _processor.postStatusMessage("Bank dump");
        return;
    }

    if (msgType != SYSEX_MSG_SCENE_BANK_LOAD) {
        return;
    }

    if (_sysexRxLength < 7) {
        sendSceneBankStatusSysEx(SCENE_BANK_STATUS_BAD_SIZE, 0);
        return;
    }

    uint8_t version = _sysexRxBuffer[3];
    uint8_t sceneCount = _sysexRxBuffer[4];
    uint8_t sceneSize = _sysexRxBuffer[5];
    size_t encodedSize = _sysexRxLength - 7;

    if (version != DMXState::SCENE_BANK_WIRE_VERSION) {
        sendSceneBankStatusSysEx(SCENE_BANK_STATUS_BAD_VERSION, version);
        return;
    }

    if (sceneCount == 0 || sceneCount > MAX_SCENES || sceneSize != DMXState::SCENE_WIRE_SIZE) {
        sendSceneBankStatusSysEx(SCENE_BANK_STATUS_BAD_SIZE, sceneCount);
        return;
    }

    size_t expectedEncodedSize = static_cast<size_t>(sceneCount) * sceneSize * 2;
    if (encodedSize != expectedEncodedSize) {
        sendSceneBankStatusSysEx(SCENE_BANK_STATUS_BAD_SIZE, sceneCount);
        return;
    }

    size_t decodedSize = decodeNibbleBytes(_sysexRxBuffer + 6,
                                           encodedSize,
                                           _sceneBankBuffer,
                                           sizeof(_sceneBankBuffer));
    if (decodedSize == 0) {
        sendSceneBankStatusSysEx(SCENE_BANK_STATUS_DECODE_ERROR, sceneCount);
        return;
    }

    if (!_dmxState || !_dmxState->deserializeSceneBank(_sceneBankBuffer, decodedSize, sceneCount)) {
        sendSceneBankStatusSysEx(SCENE_BANK_STATUS_APPLY_ERROR, sceneCount);
        return;
    }

    sendSceneBankStatusSysEx(SCENE_BANK_STATUS_OK, sceneCount);
    _processor.postStatusMessage("Bank loaded");
}

void MIDIHandler::sendSceneBankDumpSysEx() {
    if (!_dmxState) {
        sendSceneBankStatusSysEx(SCENE_BANK_STATUS_APPLY_ERROR, 0);
        return;
    }

    size_t rawSize = _dmxState->serializeSceneBank(_sceneBankBuffer, sizeof(_sceneBankBuffer));
    if (rawSize != DMXState::SCENE_BANK_WIRE_SIZE) {
        sendSceneBankStatusSysEx(SCENE_BANK_STATUS_APPLY_ERROR, 0);
        return;
    }

    _midi.write(0xF0);
    _midi.write(SYSEX_MANUFACTURER_ID);
    _midi.write(SYSEX_MSG_SCENE_BANK_DUMP);
    _midi.write(DMXState::SCENE_BANK_WIRE_VERSION);
    _midi.write(MAX_SCENES & 0x7F);
    _midi.write(DMXState::SCENE_WIRE_SIZE & 0x7F);

    for (size_t i = 0; i < rawSize; ++i) {
        uint8_t value = _sceneBankBuffer[i];
        _midi.write((value >> 4) & 0x0F);
        _midi.write(value & 0x0F);
        // Yield to the USB task every 64 raw bytes so the TX FIFO
        // does not overflow silently on large SysEx payloads.
        if ((i & 0x3F) == 0x3F) {
            delay(1);
        }
    }

    _midi.write(0xF7);
    delay(1); // ensure the final packet is flushed before returning
}

void MIDIHandler::sendSceneBankStatusSysEx(uint8_t status, uint8_t detail) {
    _midi.write(0xF0);
    _midi.write(SYSEX_MANUFACTURER_ID);
    _midi.write(SYSEX_MSG_SCENE_BANK_STATUS);
    _midi.write(status & 0x7F);
    _midi.write(detail & 0x7F);
    _midi.write(0xF7);
}

void MIDIHandler::update() {
    midiEventPacket_t packet;
    
    while (_midi.readPacket(&packet)) {
        uint8_t cin = packet.header & 0x0F;
        
        switch (cin) {
            case 0x0B: // Control Change
                _processor.handleControlChange((packet.byte1 & 0x0F) + 1, packet.byte2, packet.byte3);
                break;
                
            case 0x09: // Note On
                if (packet.byte3 > 0) {
                    _processor.handleNoteOn((packet.byte1 & 0x0F) + 1, packet.byte2, packet.byte3);
                } else {
                    _processor.handleNoteOff((packet.byte1 & 0x0F) + 1, packet.byte2, 0);
                }
                break;
                
            case 0x08: // Note Off
                _processor.handleNoteOff((packet.byte1 & 0x0F) + 1, packet.byte2, packet.byte3);
                break;

            case 0x04: // SysEx start/continue
            case 0x05: // SysEx end with 1 byte
            case 0x06: // SysEx end with 2 bytes
            case 0x07: // SysEx end with 3 bytes
                processSysExPacket(packet, cin);
                break;
        }
    }
    
    // Periodic SysEx state broadcast
    uint32_t now = millis();
    if (now - _lastSysExSend >= SYSEX_SEND_INTERVAL) {
        sendStateSysEx();
        _lastSysExSend = now;
    }
    if (now - _lastRigHealthSend >= RIG_HEALTH_SEND_INTERVAL) {
        sendRigHealthSysEx();
        _lastRigHealthSend = now;
    }
}
