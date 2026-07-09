// Copyright 2026 Paiza. All rights reserved.
//
// UTF-8 <-> UTF-16 conversion helpers used across process boundaries.

#ifndef PAIZA_COMMON_STRING_UTILS_H_
#define PAIZA_COMMON_STRING_UTILS_H_

#include <string>
#include <string_view>

namespace paiza {

// Converts a UTF-8 byte sequence to a UTF-16 wide string. Invalid input is
// replaced with U+FFFD by the underlying Win32 conversion.
std::wstring Utf8ToWide(std::string_view utf8);

// Converts a UTF-16 wide string to UTF-8 bytes.
std::string WideToUtf8(std::wstring_view wide);

}  // namespace paiza

#endif  // PAIZA_COMMON_STRING_UTILS_H_
