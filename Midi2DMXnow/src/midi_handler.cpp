#include "midi_handler.h"
#include "dmx_state.h"
#include "display_handler.h"
#include "heartbeat_collector.h"
#include "config.h"
#include <USB.h>

namespace {

constexpr uint8_t USB_MIDI_CIN_SYSEX_START_CONTINUE = 0x04;
constexpr uint8_t USB_MIDI_CIN_SYSEX_END_1 = 0x05;
constexpr uint8_t USB_MIDI_CIN_SYSEX_END_2 = 0x06;
constexpr uint8_t USB_MIDI_CIN_SYSEX_END_3 = 0x07;

constexpr uint8_t SCENE_BANK_STATUS_OK = 0;
constexpr uint8_t SCENE_BANK_STATUS_BAD_VERSION = 1;
constexpr uint8_t SCENE_BANK_STATUS_BAD_SIZE = 2;
constexpr uint8_t SCENE_BANK_STATUS_DECODE_ERROR = 3;
constexpr uint8_t SCENE_BANK_STATUS_APPLY_ERROR = 4;

void sendSysEx(USBMIDI& midi, const uint8_t* data, size_t length) {
    if (!data || length < 2) {
        return;
    }

    size_t offset = 0;
    while (offset < length) {
        const size_t remaining = length - offset;
        midiEventPacket_t packet = {};

        if (remaining > 3) {
            packet.header = USB_MIDI_CIN_SYSEX_START_CONTINUE;
            packet.byte1 = data[offset];
            packet.byte2 = data[offset + 1];
            packet.byte3 = data[offset + 2];
            offset += 3;
        } else {
            packet.header = static_cast<uint8_t>(USB_MIDI_CIN_SYSEX_END_1 + (remaining - 1));
            packet.byte1 = data[offset];
            packet.byte2 = (remaining >= 2) ? data[offset + 1] : 0;
            packet.byte3 = (remaining == 3) ? data[offset + 2] : 0;
            offset = length;
        }

        midi.writePacket(&packet);
    }
}

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
    _suspendPeriodicSysExUntil(0),
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
    uint8_t sysex[21];
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
    sysex[20] = 0xF7;

    sendSysEx(_midi, sysex, sizeof(sysex));
}

void MIDIHandler::sendRigHealthSysEx() {
    if (!_heartbeats) return;

    // Per-slot payload: nid[0..2] (masked to 7 bits), status (0-4),
    // fps (0-127), resetReason (0-127).  Always emit all 8 slots so the
    // desktop GUI sees a stable slot order that matches the master display.
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

    const uint8_t count = HeartbeatCollector::MAX_SLAVES;
    uint8_t* p = buf + 4;  // reserve byte 3 for count
    for (uint8_t i = 0; i < HeartbeatCollector::MAX_SLAVES; ++i) {
        const auto& s = slots[i];
        if (s.used) {
            p[0] = s.nid[0] & 0x7F;
            p[1] = s.nid[1] & 0x7F;
            p[2] = s.nid[2] & 0x7F;
            p[3] = static_cast<uint8_t>(statusFromSnapshot(s));
            p[4] = s.last.fps & 0x7F;
            p[5] = s.last.lastResetReason & 0x7F;
        } else {
            p[0] = 0;
            p[1] = 0;
            p[2] = 0;
            p[3] = static_cast<uint8_t>(HeartbeatCollector::EMPTY);
            p[4] = 0;
            p[5] = 0;
        }
        p += 6;
    }
    buf[3] = count;
    *p++ = 0xF7;

    const size_t totalLen = static_cast<size_t>(p - buf);
    sendSysEx(_midi, buf, totalLen);
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

    uint8_t message[6 + (DMXState::SCENE_BANK_WIRE_SIZE * 2) + 1];
    size_t length = 0;
    message[length++] = 0xF0;
    message[length++] = SYSEX_MANUFACTURER_ID;
    message[length++] = SYSEX_MSG_SCENE_BANK_DUMP;
    message[length++] = DMXState::SCENE_BANK_WIRE_VERSION;
    message[length++] = MAX_SCENES & 0x7F;
    message[length++] = DMXState::SCENE_WIRE_SIZE & 0x7F;

    for (size_t i = 0; i < rawSize; ++i) {
        uint8_t value = _sceneBankBuffer[i];
        message[length++] = (value >> 4) & 0x0F;
        message[length++] = value & 0x0F;
    }

    message[length++] = 0xF7;
    sendSysEx(_midi, message, length);

    // A bank dump is the longest SysEx transfer we send. Keep periodic state
    // feedback off the wire briefly afterwards so CoreMIDI only has to
    // reassemble one long message at a time.
    const uint32_t now = millis();
    _lastSysExSend = now;
    _lastRigHealthSend = now;
    _suspendPeriodicSysExUntil = now + SCENE_BANK_TX_QUIET_MS;
}

void MIDIHandler::sendSceneBankStatusSysEx(uint8_t status, uint8_t detail) {
    const uint8_t message[] = {
        0xF0,
        SYSEX_MANUFACTURER_ID,
        SYSEX_MSG_SCENE_BANK_STATUS,
        static_cast<uint8_t>(status & 0x7F),
        static_cast<uint8_t>(detail & 0x7F),
        0xF7,
    };
    sendSysEx(_midi, message, sizeof(message));
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
    
    // Periodic SysEx state broadcast.
    // IMPORTANT: never send both messages in the same update() call.
    // Back-to-back SysEx bursts (~56 bytes) exceed the tinyusb TX FIFO
    // (64 bytes / one bulk packet), causing silent byte drops that corrupt
    // slot alignment in the rig-health payload and the scene byte in the
    // state dump. Rig health takes priority; when it fires we defer the
    // state dump by resetting its timer, keeping them perpetually staggered.
    uint32_t now = millis();
    if (now < _suspendPeriodicSysExUntil) {
        return;
    }

    if (now - _lastRigHealthSend >= RIG_HEALTH_SEND_INTERVAL) {
        sendRigHealthSysEx();
        _lastRigHealthSend = now;
        _lastSysExSend = now;  // defer state dump — never overlap in same call
    } else if (now - _lastSysExSend >= SYSEX_SEND_INTERVAL) {
        sendStateSysEx();
        _lastSysExSend = now;
    }
}
