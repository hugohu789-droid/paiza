// Copyright 2026 Paiza. All rights reserved.
//
// Per-font calibration for vertical Mongolian stem centring. Each entry
// maps a font family to its stem ratio — the distance of the Mongolian
// stem (NIRUGU / vertical baseline) from the leading edge of a
// leading-aligned vertical layout, as a fraction of the font size.
//
// Only fonts with an entry get stem-centred rendering; fonts without one
// are drawn with standard (bounding-box centred) vertical layout and no
// adjustment. Users can calibrate additional fonts by editing
// <UserConfigDirectory>/fontstems.txt (hot-reloaded, like keymap.txt):
//
//   # <font family> <TAB> <stem ratio 0..1>
//   Noto Sans Mongolian	0.672
//
// Platform-neutral (std::filesystem); shared by the Windows D2D renderer
// and the macOS CoreText candidate panel.

#ifndef PAIZA_CORE_FONT_STEM_CONFIG_H_
#define PAIZA_CORE_FONT_STEM_CONFIG_H_

#include <optional>
#include <string>

namespace paiza {

// Full path of the user config file: <UserConfigDirectory>/fontstems.txt.
std::wstring FontStemConfigFilePath();

// Writes the built-in defaults to the config file if it does not exist,
// so users can discover and edit it. Called by a single owner process
// (Windows: the service; macOS: the input method app) at startup.
void EnsureFontStemConfigFile();

// Returns the stem ratio for `family` (case-insensitive), or nullopt when
// the font has no calibration entry. Re-reads the config file when its
// timestamp changes (checks throttled to every 500 ms). Thread-safe.
std::optional<float> GetFontStemRatio(const std::wstring& family);

}  // namespace paiza

#endif  // PAIZA_CORE_FONT_STEM_CONFIG_H_
