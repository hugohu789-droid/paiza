// Copyright 2026 Paiza. All rights reserved.

#include "common/logging.h"

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <iterator>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace paiza {
namespace {

const wchar_t* LevelTag(LogLevel level) {
  switch (level) {
    case LogLevel::kInfo:
      return L"INFO";
    case LogLevel::kWarning:
      return L"WARN";
    case LogLevel::kError:
      return L"ERROR";
  }
  return L"?";
}

}  // namespace

void Log(LogLevel level, const std::wstring& message) {
  std::wstring line = L"[PaizaIME][";
  line += LevelTag(level);
  line += L"] ";
  line += message;
  line += L"\n";
#if defined(_WIN32)
  // Visible in DebugView / the VS output window from any host process.
  ::OutputDebugStringW(line.c_str());
#else
  std::fputws(line.c_str(), stderr);
#endif
}

void Logf(LogLevel level, const wchar_t* format, ...) {
  wchar_t buffer[1024];
  va_list args;
  va_start(args, format);
  const int written =
      std::vswprintf(buffer, std::size(buffer), format, args);
  va_end(args);
  if (written < 0) buffer[std::size(buffer) - 1] = L'\0';
  Log(level, buffer);
}

}  // namespace paiza
