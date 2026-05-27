#include "heartbeat_collector.h"

#include <string.h>

bool HeartbeatCollector::ingest(const uint8_t* data, int len, uint32_t nowMs) {
    if (!data || len < (int)sizeof(HeartbeatPacket)) return false;
    if (data[0] != PACKET_TYPE_HEARTBEAT) return false;
    if (data[1] != LESLIE_HEARTBEAT_VERSION) return false;

    HeartbeatPacket pkt;
    memcpy(&pkt, data, sizeof(pkt));

    int idx = findSlot(pkt.nid);
    if (idx < 0) {
        idx = findFreeSlot();
        if (idx < 0) idx = findOldestSlot();
        if (idx < 0) return false;
        _slots[idx].used = true;
        memcpy(_slots[idx].nid, pkt.nid, 3);
    }

    _slots[idx].last = pkt;
    _slots[idx].lastHeardLocalMs = nowMs;
    return true;
}

uint8_t HeartbeatCollector::activeCount(uint32_t nowMs) const {
    uint8_t n = 0;
    for (const auto& s : _slots) {
        if (s.used && (nowMs - s.lastHeardLocalMs) < AGE_OUT_MS) n++;
    }
    return n;
}

HeartbeatCollector::Status HeartbeatCollector::statusOf(const Slot& s, uint32_t nowMs) const {
    if (!s.used) return EMPTY;
    uint32_t age = nowMs - s.lastHeardLocalMs;
    if (age >= LOST_MS) return LOST;
    if (age >= STALE_MS) return STALE;
    return OK;
}

void HeartbeatCollector::prune(uint32_t nowMs) {
    for (auto& s : _slots) {
        if (s.used && (nowMs - s.lastHeardLocalMs) > AGE_OUT_MS) {
            s.used = false;
            memset(s.nid, 0, 3);
            memset(&s.last, 0, sizeof(s.last));
        }
    }
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
