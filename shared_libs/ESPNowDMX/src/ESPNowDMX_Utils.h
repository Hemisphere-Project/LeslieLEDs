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

extern "C" {
#include "heatshrink/heatshrink_encoder.h"
#include "heatshrink/heatshrink_decoder.h"
}

// Heatshrink compression with automatic fallback to raw if not beneficial
// Returns compressed size, or 0 on error
static inline size_t compressData(const uint8_t* input, size_t inputLen, uint8_t* output, size_t maxOutputLen) {
  if (inputLen == 0 || inputLen > maxOutputLen) return 0;
  
  // Static allocation for embedded use
  static heatshrink_encoder hse;
  heatshrink_encoder_reset(&hse);
  
  size_t sunk = 0;
  size_t polled = 0;
  size_t count = 0;
  
  // Sink all input data
  HSE_sink_res sres = heatshrink_encoder_sink(&hse, (uint8_t*)input, inputLen, &sunk);
  if (sres < 0 || sunk != inputLen) {
    return 0; // Compression failed
  }
  
  // Finish encoding
  HSE_finish_res fres = heatshrink_encoder_finish(&hse);
  if (fres < 0) return 0;
  
  // Poll for compressed output
  while (fres == HSER_FINISH_MORE || polled == 0) {
    HSE_poll_res pres = heatshrink_encoder_poll(&hse, &output[polled], maxOutputLen - polled, &count);
    if (pres < 0) return 0;
    polled += count;
    
    if (pres == HSER_POLL_EMPTY) {
      fres = heatshrink_encoder_finish(&hse);
      if (fres < 0) return 0;
    }
  }
  
  // Only return compressed data if it's actually smaller
  if (polled >= inputLen) {
    return 0; // Not beneficial, caller should use raw
  }
  
  return polled;
}

// Heatshrink decompression
// Returns decompressed size, or 0 on error
static inline size_t decompressData(const uint8_t* input, size_t inputLen, uint8_t* output, size_t maxOutputLen) {
  if (inputLen == 0 || inputLen > maxOutputLen) return 0;
  
  // Static allocation for embedded use
  static heatshrink_decoder hsd;
  heatshrink_decoder_reset(&hsd);
  
  size_t sunk = 0;
  size_t polled = 0;
  size_t count = 0;
  
  // Sink compressed input data
  HSD_sink_res sres = heatshrink_decoder_sink(&hsd, (uint8_t*)input, inputLen, &sunk);
  if (sres < 0 || sunk != inputLen) {
    return 0; // Decompression failed
  }
  
  // Finish decoding
  HSD_finish_res fres = heatshrink_decoder_finish(&hsd);
  if (fres < 0) return 0;
  
  // Poll for decompressed output
  while (fres == HSDR_FINISH_MORE || polled == 0) {
    HSD_poll_res pres = heatshrink_decoder_poll(&hsd, &output[polled], maxOutputLen - polled, &count);
    if (pres < 0) return 0;
    polled += count;
    
    if (pres == HSDR_POLL_EMPTY) {
      fres = heatshrink_decoder_finish(&hsd);
      if (fres < 0) return 0;
      if (fres == HSDR_FINISH_DONE) break;
    }
  }
  
  return polled;
}
