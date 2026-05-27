#ifndef HEARTBEAT_COLLECTOR_H
#define HEARTBEAT_COLLECTOR_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <leslie_protocol.h>

// Per-slave health view kept on the master.
//
// Each slave broadcasts a HeartbeatPacket once per
// LESLIE_HEARTBEAT_PERIOD_MS. We keep up to MAX_SLAVES recently-heard
// nodes in a fixed-size array, keyed by their 3-byte nid (last 3 bytes
// of MAC). Slots age out after AGE_OUT_MS without a heartbeat so a
// powered-down slave eventually disappears from the display.
//
// ingest() is called from the MeshClock userCallback (Wi-Fi RX task,
// Core 0); prune/statusOf/activeCount run from the Arduino main loop
// (Core 1). All public methods hold _mux for their duration.
class HeartbeatCollector {
public:
    static constexpr uint8_t  MAX_SLAVES      = 8;
    static constexpr uint32_t NO_DMX_FRAME_MS = 3000;  // fresh HB but no DMX → orange
    static constexpr uint32_t STALE_MS        = 3000;  // missed >3 hb's → yellow
    static constexpr uint32_t LOST_MS         = 7000;  // missed >7 hb's → red
    static constexpr uint32_t AGE_OUT_MS      = 30000; // removed from view after 30 s silence

    struct Slot {
        bool used = false;
        uint8_t nid[3] = {0, 0, 0};
        HeartbeatPacket last{};
        uint32_t lastHeardLocalMs = 0;
    };

    // Returns false if the packet wasn't a valid heartbeat we accept
    // (wrong type/version/size). Thread-safe; called from Wi-Fi RX context.
    bool ingest(const uint8_t* data, int len, uint32_t nowMs);

    uint8_t activeCount(uint32_t nowMs) const;

    // Per-slot status used by the dot row. Thread-safe.
    enum Status { EMPTY, OK, NO_DMX, STALE, LOST };
    Status statusOf(uint8_t slotIdx, uint32_t nowMs) const;

    // Thread-safe snapshot: copies the internal array into out[MAX_SLAVES].
    // Call when you need both status and raw slot fields (e.g. SysEx packer).
    void copySlots(Slot out[MAX_SLAVES]) const;

    // Drop slots that haven't been heard from in AGE_OUT_MS so the
    // display reflects current reality. Cheap; can be called every loop.
    void prune(uint32_t nowMs);

private:
    Slot _slots[MAX_SLAVES];
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

    int findSlot(const uint8_t nid[3]) const;
    int findFreeSlot() const;
    int findOldestSlot() const;
};

#endif // HEARTBEAT_COLLECTOR_H
