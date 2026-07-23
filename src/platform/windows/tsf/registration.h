// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// COM / TSF registration helpers used by DllRegisterServer.

#ifndef PAIZA_TSF_REGISTRATION_H_
#define PAIZA_TSF_REGISTRATION_H_

#include <windows.h>

namespace paiza::tsf {

// HKCR\CLSID\{clsid}\InprocServer32 entries.
HRESULT RegisterComServer();
HRESULT UnregisterComServer();

// TSF language profile (makes the IME appear in Windows language settings)
// and category registration (keyboard TIP, display attribute provider...).
HRESULT RegisterTsfProfiles();
HRESULT UnregisterTsfProfiles();

}  // namespace paiza::tsf

#endif  // PAIZA_TSF_REGISTRATION_H_
