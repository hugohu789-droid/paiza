// Copyright 2026 Paiza. All rights reserved.
//
// COM class factory creating TextService instances.

#ifndef PAIZA_TSF_CLASS_FACTORY_H_
#define PAIZA_TSF_CLASS_FACTORY_H_

#include <unknwn.h>
#include <windows.h>

namespace paiza::tsf {

class ClassFactory final : public IClassFactory {
 public:
  ClassFactory();
  ClassFactory(const ClassFactory&) = delete;
  ClassFactory& operator=(const ClassFactory&) = delete;

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // IClassFactory
  STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid,
                              void** object) override;
  STDMETHODIMP LockServer(BOOL lock) override;

 private:
  ~ClassFactory();
  LONG ref_count_ = 1;
};

}  // namespace paiza::tsf

#endif  // PAIZA_TSF_CLASS_FACTORY_H_
