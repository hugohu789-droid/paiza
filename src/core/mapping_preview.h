// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Builds the localized, human-readable key-mapping preview shown in the
// settings dialogs (Windows and macOS render the same text).

#ifndef PAIZA_CORE_MAPPING_PREVIEW_H_
#define PAIZA_CORE_MAPPING_PREVIEW_H_

#include <string>

#include "core/key_mapping.h"
#include "core/localization.h"

namespace paiza {

// Compact multi-line preview of `mapping`: rows of lower-case keys, one
// row with the Shift second set, and the multi-letter sequences. Row
// labels come from the localized strings.
std::wstring BuildMappingPreview(const KeyMapping& mapping,
                                 const UiStrings& strings);

}  // namespace paiza

#endif  // PAIZA_CORE_MAPPING_PREVIEW_H_
