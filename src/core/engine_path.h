// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Resolving a profile's `engine` field to an executable to run.
//
// This is the highest-risk line in the engine contract. An input profile is a
// plain text file; if it could name an arbitrary path, then anything able to
// write a profile could run anything. So the field is treated as a bare
// *name* and resolved inside the install directory only -- never through
// PATH, never with a path of its own.
//
// The checks are deliberately a whitelist-ish set of rejections rather than
// an attempt to sanitise: a rejected profile is a visible failure, whereas a
// silently rewritten one is a vulnerability waiting to be found.

#ifndef PAIZA_CORE_ENGINE_PATH_H_
#define PAIZA_CORE_ENGINE_PATH_H_

#include <string>

namespace paiza {

// True when `name` is acceptable as an engine executable name. On rejection,
// *reason (when given) receives a short explanation for the log and the
// settings page.
//
// Rejected: anything empty; path separators; "." and ".."; drive letters and
// colons (which also covers NTFS alternate data streams); a leading '-',
// which a command line could read as an option; control characters and
// quotes; a trailing dot or space, which Windows strips, so that a check
// could otherwise be bypassed; and the Windows reserved device names (CON,
// NUL, COM1 and friends), which open a device rather than a file.
bool IsAcceptableEngineName(const std::wstring& name, std::string* reason);

// The platform's executable suffix. A profile file is shared by all three
// platforms while an executable name is not -- the same engine is
// `zcode_stdio` on macOS and Linux and `zcode_stdio.exe` on Windows -- so a
// profile names it without an extension and the host appends this.
//
// It has to be the host that does it: CreateProcessW's lpApplicationName
// assumes no default extension, so a bare name simply fails to start there.
#ifdef _WIN32
inline constexpr const wchar_t* kEngineExecutableSuffix = L".exe";
#else
inline constexpr const wchar_t* kEngineExecutableSuffix = L"";
#endif

// Joins an accepted name onto `install_dir`, appending
// kEngineExecutableSuffix unless the name already ends with it. Returns an
// empty string when the name is rejected or the directory is empty, with
// *reason set.
//
// Existence is not checked here: whether the file is there is a question for
// the moment of launching, and keeping this function free of the filesystem
// keeps the security-critical part exhaustively testable.
std::wstring ResolveEnginePath(const std::wstring& install_dir,
                               const std::wstring& name, std::string* reason);

}  // namespace paiza

#endif  // PAIZA_CORE_ENGINE_PATH_H_
