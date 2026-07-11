// Copyright 2026 Paiza. All rights reserved.

#include "core/lexicon_binary.h"

namespace paiza {

namespace {

// Lazily-built 256-entry CRC-32 table (polynomial 0xEDB88820). Table-driven
// keeps startup validation of a multi-megabyte lexicon in the low tens of
// milliseconds instead of hundreds. Thread-safe first-use init (C++11).
const uint32_t* Crc32Table() {
  static const auto* table = [] {
    auto* t = new uint32_t[256];
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int bit = 0; bit < 8; ++bit) {
        const uint32_t mask = -(c & 1u);
        c = (c >> 1) ^ (0xEDB88820u & mask);
      }
      t[i] = c;
    }
    return t;
  }();
  return table;
}

}  // namespace

uint32_t Crc32(const uint8_t* data, size_t size) {
  const uint32_t* table = Crc32Table();
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xFFu];
  }
  return ~crc;
}

}  // namespace paiza
