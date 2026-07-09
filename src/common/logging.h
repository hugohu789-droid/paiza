// Copyright 2026 Paiza. All rights reserved.
//
// Minimal debug logging. Output goes to the debugger (OutputDebugString) so
// it is visible in DebugView / VS output window from any host process the
// TSF DLL is loaded into. Intentionally dependency-free and cheap when no
// debugger is attached.

#ifndef PAIZA_COMMON_LOGGING_H_
#define PAIZA_COMMON_LOGGING_H_

#include <string>

namespace paiza {

enum class LogLevel {
  kInfo,
  kWarning,
  kError,
};

// Writes a single log line, prefixed with "[PaizaIME]" and the level tag.
void Log(LogLevel level, const std::wstring& message);

// printf-style convenience overload.
void Logf(LogLevel level, const wchar_t* format, ...);

}  // namespace paiza

#define PAIZA_LOG_INFO(msg) ::paiza::Log(::paiza::LogLevel::kInfo, (msg))
#define PAIZA_LOG_WARN(msg) ::paiza::Log(::paiza::LogLevel::kWarning, (msg))
#define PAIZA_LOG_ERROR(msg) ::paiza::Log(::paiza::LogLevel::kError, (msg))

#endif  // PAIZA_COMMON_LOGGING_H_
