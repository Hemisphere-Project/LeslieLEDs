#include "heartbeat_collector.h"

#include <string.h>

bool HeartbeatCollector::ingest(const uint8_t* data, int len, uint32_t nowMs) {
    if (!data || len < (int)sizeof(HeartbeatPacket)) return false;
    if (data[0] != PACKET_TYPE_HEARTBEAT) return false;
    if (data[1] != LESLIE_HEARTBEAT_VERSION) return false;

    HeartbeatPacket pkt;
    memcpy(&pkt, data, sizeof(pkt));

    portENTER_CRITICAL(&_mux);
    int idx = findSlot(pkt.nid);
    if (idx < 0) {
        idx = findFreeSlot();
        if (idx < 0) idx = findOldestSlot();
        if (idx < 0) {
            portEXIT_CRITICAL(&_mux);
            return false;
        }
        _slots[idx].used = true;
        memcpy(_slots[idx].nid, pkt.nid, 3);
    }
    _slots[idx].last = pkt;
    _slots[idx].lastHeardLocalMs = nowMs;
    portEXIT_CRITICAL(&_mux);
    return true;
}

uint8_t HeartbeatCollector::activeCount(uint32_t nowMs) const {
    uint8_t n = 0;
    portENTER_CRITICAL(&_mux);
    for (const auto& s : _slots) {
        if (s.used && (nowMs - s.lastHeardLocalMs) < AGE_OUT_MS) n++;
    }
    portEXIT_CRITICAL(&_mux);
    return n;
}

HeartbeatCollector::Status HeartbeatCollector::statusOf(uint8_t slotIdx, uint32_t nowMs) const {
    if (slotIdx >= MAX_SLAVES) return EMPTY;
    portENTER_CRITICAL(&_mux);
    const Slot& s = _slots[slotIdx];
    if (!s.used) {
        portEXIT_CRITICAL(&_mux);
        return EMPTY;
    }
    uint32_t age = nowMs - s.lastHeardLocalMs;
    uint32_t msSinceFrame = s.last.msSinceLastFrame;
    portEXIT_CRITICAL(&_mux);

    if (age >= LOST_MS)  return LOST;
    if (age >= STALE_MS) return STALE;
    // Heartbeat is fresh — check whether DMX is actually flowing to this slave.
    if (msSinceFrame >= NO_DMX_FRAME_MS) return NO_DMX;
    return OK;
}

void HeartbeatCollector::copySlots(Slot out[MAX_SLAVES]) const {
    portENTER_CRITICAL(&_mux);
    memcpy(out, _slots, sizeof(_slots));
    portEXIT_CRITICAL(&_mux);
}

void HeartbeatCollector::prune(uint32_t nowMs) {
    portENTER_CRITICAL(&_mux);
    for (auto& s : _slots) {
        if (s.used && (nowMs - s.lastHeardLocalMs) > AGE_OUT_MS) {
            s.used = false;
            memset(s.nid, 0, 3);
            memset(&s.last, 0, sizeof(s.last));
        }
    }
    portEXIT_CRITICAL(&_mux);
}

int HeartbeatCollector::findSlot(const uint8_t nid[3]) const {
    for (uint8_t i = 0; i < MAX_SLAVES; i++) {
        if (_slots[i].used && memcmp(_slots[i].nid, nid, 3) == 0) return i;
    }
    return -1;
}

int HeartbeatCollector::findFreeSlot() const {
    for (uint8_t i = 0; i < MAX_SLAVES; i++) {
        if (!_slots[i].used) return i;
    }
    return -1;
}

int HeartbeatCollector::findOldestSlot() const {
    int oldest = -1;
    uint32_t oldestTime = UINT32_MAX;
    for (uint8_t i = 0; i < MAX_SLAVES; i++) {
        if (_slots[i].used && _slots[i].lastHeardLocalMs < oldestTime) {
            oldest = i;
            oldestTime = _slots[i].lastHeardLocalMs;
        }
    }
    return oldest;
}
