// Copyright 2026 Paiza. All rights reserved.
//
// Per-platform filesystem locations shared by all IME components.

#ifndef PAIZA_COMMON_PATHS_H_
#define PAIZA_COMMON_PATHS_H_

#include <string>

namespace paiza {

// Per-user configuration directory (not created automatically):
//   Windows:  %APPDATA%\PaizaIME
//   macOS:    ~/Library/Application Support/PaizaIME
//   Linux:    $XDG_CONFIG_HOME/paiza-ime (or ~/.config/paiza-ime)
// Empty when the base location cannot be resolved.
std::wstring UserConfigDirectory();

}  // namespace paiza

#endif  // PAIZA_COMMON_PATHS_H_
