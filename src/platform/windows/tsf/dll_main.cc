// Copyright 2026 Paiza. All rights reserved.
//
// DLL entry point and the four COM server exports.

#include <windows.h>

#include <atomic>

#include "tsf/class_factory.h"
#include "tsf/guids.h"
#include "tsf/module.h"
#include "tsf/registration.h"

namespace paiza::tsf {
namespace {

std::atomic<LONG> g_module_ref_count{0};
HINSTANCE g_module_instance = nullptr;

}  // namespace

void ModuleAddRef() { ++g_module_ref_count; }
void ModuleRelease() { --g_module_ref_count; }
LONG ModuleRefCount() { return g_module_ref_count.load(); }

HINSTANCE ModuleInstance() { return g_module_instance; }
void SetModuleInstance(HINSTANCE instance) { g_module_instance = instance; }

std::wstring ModulePath() {
  wchar_t path[MAX_PATH] = {};
  ::GetModuleFileNameW(g_module_instance, path, MAX_PATH);
  return path;
}

std::wstring ModuleDirectory() {
  std::wstring path = ModulePath();
  const size_t slash = path.find_last_of(L'\\');
  return slash == std::wstring::npos ? path : path.substr(0, slash);
}

}  // namespace paiza::tsf

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason,
                               LPVOID reserved) {
  (void)reserved;
  switch (reason) {
    case DLL_PROCESS_ATTACH:
      paiza::tsf::SetModuleInstance(instance);
      ::DisableThreadLibraryCalls(instance);
      break;
    default:
      break;
  }
  return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** object) {
  if (object == nullptr) return E_INVALIDARG;
  *object = nullptr;
  if (!::IsEqualCLSID(rclsid, paiza::tsf::kClsidTextService)) {
    return CLASS_E_CLASSNOTAVAILABLE;
  }
  auto* factory = new paiza::tsf::ClassFactory();
  const HRESULT hr = factory->QueryInterface(riid, object);
  factory->Release();
  return hr;
}

STDAPI DllCanUnloadNow() {
  return paiza::tsf::ModuleRefCount() == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
  HRESULT hr = paiza::tsf::RegisterComServer();
  if (FAILED(hr)) return hr;
  hr = paiza::tsf::RegisterTsfProfiles();
  if (FAILED(hr)) {
    paiza::tsf::UnregisterComServer();
    return hr;
  }
  return S_OK;
}

STDAPI DllUnregisterServer() {
  paiza::tsf::UnregisterTsfProfiles();
  paiza::tsf::UnregisterComServer();
  return S_OK;
}
