/*
 * ESPNowDMX - DMX over ESP-NOW for ESP32
 * Copyright (c) 2025 maigre, Hemisphere-Project
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "ESPNowDMX_Common.h"
#include "ESPNowDMX_Utils.h"

class ESPNowDMX_Receiver {
public:
  ESPNowDMX_Receiver();
  bool begin(bool registerInternalEspNow = true);

  void setDMXReceiveCallback(DMXReceiveCallback cb);
  void setUniverseId(uint8_t universe);
  bool handleReceive(const uint8_t *mac, const uint8_t *data, int len);

private:
  uint8_t dmxBuffer[DMX_UNIVERSE_SIZE];
  uint16_t lastSequence;
  bool hasLastSequence;
  DMXReceiveCallback userCallback;
  bool espNowInitialized;
  uint8_t universeId;

  void processPacket(const uint8_t *data, int len);

  static ESPNowDMX_Receiver* instance;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  static void onDataReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len);
#else
  static void onDataReceived(const uint8_t *mac, const uint8_t *data, int len);
#endif
};
