// Copyright 2026 Paiza. All rights reserved.
//
// DLL module bookkeeping shared by the COM boilerplate.

#ifndef PAIZA_TSF_MODULE_H_
#define PAIZA_TSF_MODULE_H_

#include <windows.h>

#include <string>

namespace paiza::tsf {

// Global COM object/lock count controlling DllCanUnloadNow.
void ModuleAddRef();
void ModuleRelease();
LONG ModuleRefCount();

HINSTANCE ModuleInstance();
void SetModuleInstance(HINSTANCE instance);

// Full path of the DLL, and the directory containing it.
std::wstring ModulePath();
std::wstring ModuleDirectory();

}  // namespace paiza::tsf

#endif  // PAIZA_TSF_MODULE_H_
