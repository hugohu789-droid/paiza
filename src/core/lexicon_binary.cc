// Copyright 2026 Paiza. All rights reserved.

#include "core/lexicon_binary.h"

namespace paiza {

uint32_t Crc32(const uint8_t* data, size_t size) {
  // Table-free Sarwate-style CRC-32 (polynomial 0xEDB88820). Small and
  // dependency-free; the lexicon is validated once at load, so speed is a
  // non-issue.
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88820u & mask);
    }
  }
  return ~crc;
}

}  // namespace paiza
