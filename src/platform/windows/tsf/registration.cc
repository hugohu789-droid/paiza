// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "tsf/registration.h"

#include <msctf.h>
#include <wrl/client.h>

#include <string>

#include "tsf/guids.h"
#include "tsf/module.h"

using Microsoft::WRL::ComPtr;

namespace paiza::tsf {
namespace {

std::wstring GuidToString(REFGUID guid) {
  wchar_t buffer[64] = {};
  ::StringFromGUID2(guid, buffer, static_cast<int>(_countof(buffer)));
  return buffer;
}

// TSF categories this TIP belongs to. GUID_TFCAT_TIP_KEYBOARD makes it a
// keyboard TIP; the TIPCAP entries declare Windows 8+ capabilities
// (immersive/UWP support, systray integration, COM-less activation).
const GUID* kCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    &GUID_TFCAT_TIPCAP_COMLESS,
    &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
};

}  // namespace

HRESULT RegisterComServer() {
  const std::wstring clsid = GuidToString(kClsidTextService);
  const std::wstring key_path = L"CLSID\\" + clsid;
  const std::wstring inproc_path = key_path + L"\\InprocServer32";
  const std::wstring dll_path = ModulePath();

  HKEY key = nullptr;
  LONG result = ::RegCreateKeyExW(HKEY_CLASSES_ROOT, key_path.c_str(), 0,
                                  nullptr, REG_OPTION_NON_VOLATILE,
                                  KEY_WRITE, nullptr, &key, nullptr);
  if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
  ::RegSetValueExW(key, nullptr, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(kServiceDescription),
                   static_cast<DWORD>((wcslen(kServiceDescription) + 1) *
                                      sizeof(wchar_t)));
  ::RegCloseKey(key);

  result = ::RegCreateKeyExW(HKEY_CLASSES_ROOT, inproc_path.c_str(), 0,
                             nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                             nullptr, &key, nullptr);
  if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
  ::RegSetValueExW(key, nullptr, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(dll_path.c_str()),
                   static_cast<DWORD>((dll_path.size() + 1) *
                                      sizeof(wchar_t)));
  const wchar_t kApartment[] = L"Apartment";
  ::RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(kApartment),
                   sizeof(kApartment));
  ::RegCloseKey(key);
  return S_OK;
}

HRESULT UnregisterComServer() {
  const std::wstring key_path = L"CLSID\\" + GuidToString(kClsidTextService);
  ::RegDeleteTreeW(HKEY_CLASSES_ROOT, key_path.c_str());
  return S_OK;
}

HRESULT RegisterTsfProfiles() {
  ComPtr<ITfInputProcessorProfiles> profiles;
  HRESULT hr = ::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&profiles));
  if (FAILED(hr)) return hr;

  hr = profiles->Register(kClsidTextService);
  if (FAILED(hr)) return hr;

  const std::wstring icon_path = ModulePath();
  hr = profiles->AddLanguageProfile(
      kClsidTextService, kLangId, kGuidProfile, kServiceDescription,
      static_cast<ULONG>(wcslen(kServiceDescription)), icon_path.c_str(),
      static_cast<ULONG>(icon_path.size()), 0);
  if (FAILED(hr)) return hr;

  ComPtr<ITfCategoryMgr> category_mgr;
  hr = ::CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                          CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&category_mgr));
  if (FAILED(hr)) return hr;
  for (const GUID* category : kCategories) {
    // Some categories may be unsupported on older builds; keep going.
    category_mgr->RegisterCategory(kClsidTextService, *category,
                                   kClsidTextService);
  }
  return S_OK;
}

HRESULT UnregisterTsfProfiles() {
  ComPtr<ITfCategoryMgr> category_mgr;
  if (SUCCEEDED(::CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&category_mgr)))) {
    for (const GUID* category : kCategories) {
      category_mgr->UnregisterCategory(kClsidTextService, *category,
                                       kClsidTextService);
    }
  }

  ComPtr<ITfInputProcessorProfiles> profiles;
  if (SUCCEEDED(::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&profiles)))) {
    profiles->Unregister(kClsidTextService);
  }
  return S_OK;
}

}  // namespace paiza::tsf
