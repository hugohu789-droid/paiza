// Copyright 2026 Paiza. All rights reserved.

#include "tsf/class_factory.h"

#include "tsf/module.h"
#include "tsf/text_service.h"

namespace paiza::tsf {

ClassFactory::ClassFactory() { ModuleAddRef(); }

ClassFactory::~ClassFactory() { ModuleRelease(); }

STDMETHODIMP ClassFactory::QueryInterface(REFIID riid, void** object) {
  if (object == nullptr) return E_INVALIDARG;
  if (::IsEqualIID(riid, IID_IUnknown) ||
      ::IsEqualIID(riid, IID_IClassFactory)) {
    *object = static_cast<IClassFactory*>(this);
    AddRef();
    return S_OK;
  }
  *object = nullptr;
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) ClassFactory::AddRef() {
  return ::InterlockedIncrement(&ref_count_);
}

STDMETHODIMP_(ULONG) ClassFactory::Release() {
  const ULONG count = ::InterlockedDecrement(&ref_count_);
  if (count == 0) delete this;
  return count;
}

STDMETHODIMP ClassFactory::CreateInstance(IUnknown* outer, REFIID riid,
                                          void** object) {
  if (object == nullptr) return E_INVALIDARG;
  *object = nullptr;
  if (outer != nullptr) return CLASS_E_NOAGGREGATION;

  auto* service = new TextService();
  const HRESULT hr = service->QueryInterface(riid, object);
  service->Release();
  return hr;
}

STDMETHODIMP ClassFactory::LockServer(BOOL lock) {
  if (lock) {
    ModuleAddRef();
  } else {
    ModuleRelease();
  }
  return S_OK;
}

}  // namespace paiza::tsf
