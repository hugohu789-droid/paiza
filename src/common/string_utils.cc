// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Portable UTF-8 <-> wide-string conversion. wchar_t is UTF-16 on Windows
// and UTF-32 on macOS/Linux; both are handled. Invalid input decodes to
// U+FFFD (like the Win32 conversion previously used).

#include "common/string_utils.h"

#include <cstdint>

namespace paiza {
namespace {

constexpr uint32_t kReplacement = 0xFFFD;
constexpr uint32_t kMaxCodepoint = 0x10FFFF;

void AppendCodepointToWide(std::wstring* out, uint32_t cp) {
  if (cp > kMaxCodepoint || (cp >= 0xD800 && cp <= 0xDFFF)) {
    cp = kReplacement;
  }
  if constexpr (sizeof(wchar_t) == 2) {
    if (cp <= 0xFFFF) {
      out->push_back(static_cast<wchar_t>(cp));
    } else {
      cp -= 0x10000;
      out->push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
      out->push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
    }
  } else {
    out->push_back(static_cast<wchar_t>(cp));
  }
}

void AppendCodepointToUtf8(std::string* out, uint32_t cp) {
  if (cp > kMaxCodepoint || (cp >= 0xD800 && cp <= 0xDFFF)) {
    cp = kReplacement;
  }
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

}  // namespace

std::wstring Utf8ToWide(std::string_view utf8) {
  std::wstring out;
  out.reserve(utf8.size());
  size_t i = 0;
  while (i < utf8.size()) {
    const unsigned char lead = static_cast<unsigned char>(utf8[i]);
    uint32_t cp = 0;
    int continuation = 0;
    uint32_t min_value = 0;  // Overlong-encoding guard.
    if (lead < 0x80) {
      cp = lead;
    } else if ((lead & 0xE0) == 0xC0) {
      cp = lead & 0x1F;
      continuation = 1;
      min_value = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
      cp = lead & 0x0F;
      continuation = 2;
      min_value = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
      cp = lead & 0x07;
      continuation = 3;
      min_value = 0x10000;
    } else {
      AppendCodepointToWide(&out, kReplacement);
      ++i;
      continue;
    }
    if (i + continuation >= utf8.size()) {
      AppendCodepointToWide(&out, kReplacement);
      break;
    }
    bool valid = true;
    for (int k = 1; k <= continuation; ++k) {
      const unsigned char c = static_cast<unsigned char>(utf8[i + k]);
      if ((c & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      cp = (cp << 6) | (c & 0x3F);
    }
    if (!valid || cp < min_value) {
      AppendCodepointToWide(&out, kReplacement);
      ++i;  // Resync on the next byte.
      continue;
    }
    AppendCodepointToWide(&out, cp);
    i += continuation + 1;
  }
  return out;
}

std::string WideToUtf8(std::wstring_view wide) {
  std::string out;
  out.reserve(wide.size() * 3);
  for (size_t i = 0; i < wide.size(); ++i) {
    uint32_t cp = static_cast<uint32_t>(wide[i]);
    if constexpr (sizeof(wchar_t) == 2) {
      if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < wide.size()) {
        const uint32_t low = static_cast<uint32_t>(wide[i + 1]);
        if (low >= 0xDC00 && low <= 0xDFFF) {
          cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          ++i;
        }
      }
    }
    AppendCodepointToUtf8(&out, cp);
  }
  return out;
}

}  // namespace paiza
