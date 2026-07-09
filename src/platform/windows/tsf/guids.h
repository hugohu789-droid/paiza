// Copyright 2026 Paiza. All rights reserved.
//
// GUIDs identifying the Paiza Mongolian text service to TSF.

#ifndef PAIZA_TSF_GUIDS_H_
#define PAIZA_TSF_GUIDS_H_

#include <windows.h>

namespace paiza::tsf {

// COM class of the text service (registered under HKCR\CLSID).
extern const CLSID kClsidTextService;

// TSF language profile of the IME.
extern const GUID kGuidProfile;

// Display attribute applied to the composition string (dotted underline).
extern const GUID kGuidDisplayAttributeInput;

// LANGID for Mongolian (Traditional Mongolian, China): mn-Mong-CN.
inline constexpr LANGID kLangId = 0x0850;

inline constexpr wchar_t kServiceDescription[] = L"Paiza Mongolian IME";

}  // namespace paiza::tsf

#endif  // PAIZA_TSF_GUIDS_H_
