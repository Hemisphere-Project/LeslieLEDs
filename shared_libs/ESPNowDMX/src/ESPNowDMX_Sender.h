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

class ESPNowDMX_Sender {
public:
  ESPNowDMX_Sender();
  bool begin(bool registerInternalEspNow = true);
  void setUniverse(const uint8_t* dmxData);
  void setUniverseId(uint8_t universe);
  void setChannel(uint16_t address, uint8_t value);
  void loop();

private:
  uint8_t currentUniverse[DMX_UNIVERSE_SIZE];
  uint8_t prevUniverse[DMX_UNIVERSE_SIZE];
  uint16_t seqNumber;
  unsigned long lastSendTime;
  bool espNowInitialized;
  uint8_t universeId;

  void sendChunk(uint16_t offset, uint16_t length);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  static void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status);
#else
  static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
#endif
};
