// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/engine_path.h"

#include <cctype>

namespace paiza {
namespace {

void Reject(std::string* reason, const char* text) {
  if (reason != nullptr) *reason = text;
}

bool IsSeparator(wchar_t c) { return c == L'/' || c == L'\\'; }

// Windows opens these as devices wherever they appear as a filename, with or
// without an extension, in any case. Trying to run one is never what a profile
// meant, and on Windows it would not fail the way a missing file does.
bool IsReservedDeviceName(const std::wstring& name) {
  // Compare the stem only: "NUL.exe" is still the NUL device.
  const size_t dot = name.find(L'.');
  std::wstring stem = name.substr(0, dot);
  for (wchar_t& c : stem) {
    if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
  }
  if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL") {
    return true;
  }
  // COM1..COM9 and LPT1..LPT9.
  if (stem.size() == 4 && stem[3] >= L'1' && stem[3] <= L'9') {
    const std::wstring prefix = stem.substr(0, 3);
    if (prefix == L"COM" || prefix == L"LPT") return true;
  }
  return false;
}

}  // namespace

bool IsAcceptableEngineName(const std::wstring& name, std::string* reason) {
  if (name.empty()) {
    Reject(reason, "the profile names no engine");
    return false;
  }
  // A name is resolved inside the install directory, so it must not be able to
  // point outside it. This single rule is what makes the rest of the resolver
  // safe: no separator means no traversal, no absolute path, no UNC share.
  for (const wchar_t c : name) {
    if (IsSeparator(c)) {
      Reject(reason, "an engine name may not contain a path separator");
      return false;
    }
    if (c == L':') {
      // A drive letter, or an NTFS alternate data stream ("engine.exe:evil").
      Reject(reason, "an engine name may not contain ':'");
      return false;
    }
    if (c < 0x20 || c == 0x7F) {
      Reject(reason, "an engine name may not contain control characters");
      return false;
    }
    if (c == L'"' || c == L'\'' || c == L'*' || c == L'?' || c == L'<' ||
        c == L'>' || c == L'|') {
      Reject(reason, "an engine name contains a character that is not allowed");
      return false;
    }
  }
  if (name == L"." || name == L"..") {
    Reject(reason, "an engine name may not be '.' or '..'");
    return false;
  }
  if (name.front() == L'-') {
    // A command line could read a leading '-' as an option rather than a file.
    Reject(reason, "an engine name may not begin with '-'");
    return false;
  }
  // Windows silently strips these, so a name ending in one would resolve to a
  // different file than the one that was checked.
  if (name.back() == L'.' || name.back() == L' ') {
    Reject(reason, "an engine name may not end with a dot or a space");
    return false;
  }
  if (name.front() == L' ') {
    Reject(reason, "an engine name may not begin with a space");
    return false;
  }
  if (IsReservedDeviceName(name)) {
    Reject(reason, "an engine name may not be a reserved device name");
    return false;
  }
  return true;
}

std::wstring ResolveEnginePath(const std::wstring& install_dir,
                               const std::wstring& name, std::string* reason) {
  if (reason != nullptr) reason->clear();
  if (install_dir.empty()) {
    Reject(reason, "the install directory is not known");
    return std::wstring();
  }
  if (!IsAcceptableEngineName(name, reason)) return std::wstring();

  std::wstring path = install_dir;
  if (!IsSeparator(path.back())) {
#ifdef _WIN32
    path.push_back(L'\\');
#else
    path.push_back(L'/');
#endif
  }
  path += name;
  return path;
}

}  // namespace paiza
