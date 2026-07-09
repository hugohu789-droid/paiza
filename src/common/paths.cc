// Copyright 2026 Paiza. All rights reserved.

#include "common/paths.h"

#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#include <shlobj.h>
#include <windows.h>
#endif

namespace paiza {

std::wstring UserConfigDirectory() {
#if defined(_WIN32)
  PWSTR appdata = nullptr;
  std::wstring dir;
  if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                       &appdata))) {
    dir = appdata;
    dir += L"\\PaizaIME";
  }
  ::CoTaskMemFree(appdata);
  return dir;
#elif defined(__APPLE__)
  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') return {};
  return (std::filesystem::path(home) /
          "Library/Application Support/PaizaIME")
      .wstring();
#else
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  std::filesystem::path base;
  if (xdg != nullptr && *xdg != '\0') {
    base = xdg;
  } else {
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return {};
    base = std::filesystem::path(home) / ".config";
  }
  return (base / "paiza-ime").wstring();
#endif
}

}  // namespace paiza
