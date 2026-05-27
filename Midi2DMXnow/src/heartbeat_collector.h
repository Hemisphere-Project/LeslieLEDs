#ifndef HEARTBEAT_COLLECTOR_H
#define HEARTBEAT_COLLECTOR_H

#include <Arduino.h>
#include <leslie_protocol.h>

// Per-slave health view kept on the master.
//
// Each slave broadcasts a HeartbeatPacket once per
// LESLIE_HEARTBEAT_PERIOD_MS. We keep up to MAX_SLAVES recently-heard
// nodes in a fixed-size array, keyed by their 3-byte nid (last 3 bytes
// of MAC). Slots age out after AGE_OUT_MS without a heartbeat so a
// powered-down slave eventually disappears from the display.
//
// All mutation happens from the main loop (the dispatcher pulls bytes
// out of MeshClock's RX userCallback which forwards to ingest()).
// Display read is also main-loop; no cross-thread locking needed.
class HeartbeatCollector {
public:
    static constexpr uint8_t MAX_SLAVES = 8;
    static constexpr uint32_t STALE_MS = 3000;    // missed >3 hb's → yellow
    static constexpr uint32_t LOST_MS = 7000;     // missed >7 hb's → red
    static constexpr uint32_t AGE_OUT_MS = 30000; // removed from view after 30 s silence

    struct Slot {
        bool used = false;
        uint8_t nid[3] = {0, 0, 0};
        HeartbeatPacket last{};
        uint32_t lastHeardLocalMs = 0;
    };

    // Returns false if the packet wasn't a valid heartbeat we accept
    // (wrong type/version/size). Safe to call from any context but
    // the project drives it from the main loop dispatcher.
    bool ingest(const uint8_t* data, int len, uint32_t nowMs);

    // For the display layer.
    const Slot* slots() const { return _slots; }
    uint8_t activeCount(uint32_t nowMs) const;

    // Per-slot status used by the dot row.
    enum Status { EMPTY, OK, STALE, LOST };
    Status statusOf(const Slot& s, uint32_t nowMs) const;

    // Drop slots that haven't been heard from in AGE_OUT_MS so the
    // display reflects current reality. Cheap; can be called every loop.
    void prune(uint32_t nowMs);

private:
    Slot _slots[MAX_SLAVES];

    int findSlot(const uint8_t nid[3]) const;
    int findFreeSlot() const;
    int findOldestSlot() const;
};

#endif // HEARTBEAT_COLLECTOR_H
