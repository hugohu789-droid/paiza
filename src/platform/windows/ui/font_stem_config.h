// Copyright 2026 Paiza. All rights reserved.
//
// Compatibility shim: the stem calibration store moved to src/core (it is
// pure portable file parsing, shared with the macOS CoreText renderer).
// Windows UI code keeps calling it as paiza::ui::GetFontStemRatio.

#ifndef PAIZA_UI_FONT_STEM_CONFIG_H_
#define PAIZA_UI_FONT_STEM_CONFIG_H_

#include "core/font_stem_config.h"

namespace paiza::ui {

using paiza::EnsureFontStemConfigFile;
using paiza::FontStemConfigFilePath;
using paiza::GetFontStemRatio;

}  // namespace paiza::ui

#endif  // PAIZA_UI_FONT_STEM_CONFIG_H_
