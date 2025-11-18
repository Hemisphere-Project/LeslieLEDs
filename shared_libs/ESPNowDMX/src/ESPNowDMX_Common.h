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

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

#if __has_include(<esp_idf_version.h>)
#include <esp_idf_version.h>
#endif

#ifndef ESP_IDF_VERSION_MAJOR
#define ESP_IDF_VERSION_MAJOR 3
#define ESP_IDF_VERSION_MINOR 0
#define ESP_IDF_VERSION_PATCH 0
#endif

#ifndef ESP_IDF_VERSION_VAL
#define ESP_IDF_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#endif

#ifndef ESP_IDF_VERSION
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH)
#endif

// DMX universe size
constexpr uint16_t DMX_UNIVERSE_SIZE = 512;

// Max payload size for ESP-NOW
constexpr uint16_t ESP_NOW_MAX_PAYLOAD = 250;

// Packet types
enum PacketType : uint8_t {
  PACKET_TYPE_DATA_CHUNK = 0x01,
};

// Compression flags
enum CompressionType : uint8_t {
  COMPRESSION_NONE = 0x00,
  COMPRESSION_HEATSHRINK = 0x01,
};

// DMX data chunk packet header size
constexpr uint8_t PACKET_HEADER_SIZE = 7; // type(1) + universe(1) + seq(2) + offset(2) + compression(1)

// Max DMX data chunk per packet
constexpr uint16_t MAX_DMX_CHUNK_SIZE = ESP_NOW_MAX_PAYLOAD - PACKET_HEADER_SIZE;

// Type for DMX data buffer
using DMXUniverseBuffer = uint8_t[DMX_UNIVERSE_SIZE];

// User callback type for receiver to receive full buffer
using DMXReceiveCallback = void (*)(uint8_t universe, const uint8_t *dmxData);
