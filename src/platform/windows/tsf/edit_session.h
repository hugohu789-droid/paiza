// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Lambda-based ITfEditSession adapter. TSF only lets a TIP touch document
// text inside an edit session; this wrapper turns the ceremony into:
//
//   RequestEditSession(context, client_id, TF_ES_READWRITE | TF_ES_SYNC,
//                      [&](TfEditCookie ec) -> HRESULT { ... });

#ifndef PAIZA_TSF_EDIT_SESSION_H_
#define PAIZA_TSF_EDIT_SESSION_H_

#include <msctf.h>
#include <windows.h>

#include <utility>

namespace paiza::tsf {

template <typename Callback>
class EditSessionTask final : public ITfEditSession {
 public:
  explicit EditSessionTask(Callback callback)
      : callback_(std::move(callback)) {}
  EditSessionTask(const EditSessionTask&) = delete;
  EditSessionTask& operator=(const EditSessionTask&) = delete;

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
    if (object == nullptr) return E_INVALIDARG;
    if (::IsEqualIID(riid, IID_IUnknown) ||
        ::IsEqualIID(riid, IID_ITfEditSession)) {
      *object = static_cast<ITfEditSession*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override {
    return ::InterlockedIncrement(&ref_count_);
  }
  STDMETHODIMP_(ULONG) Release() override {
    const ULONG count = ::InterlockedDecrement(&ref_count_);
    if (count == 0) delete this;
    return count;
  }

  // ITfEditSession
  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    return callback_(ec);
  }

 private:
  ~EditSessionTask() = default;

  Callback callback_;
  LONG ref_count_ = 1;
};

// Runs `callback` inside an edit session on `context`. Returns the session
// HRESULT for synchronous sessions, or the request HRESULT otherwise.
template <typename Callback>
HRESULT RequestEditSession(ITfContext* context, TfClientId client_id,
                           DWORD flags, Callback callback) {
  if (context == nullptr) return E_INVALIDARG;
  auto* task = new EditSessionTask<Callback>(std::move(callback));
  HRESULT session_result = E_FAIL;
  const HRESULT hr =
      context->RequestEditSession(client_id, task, flags, &session_result);
  task->Release();
  if (FAILED(hr)) return hr;
  return session_result;
}

}  // namespace paiza::tsf

#endif  // PAIZA_TSF_EDIT_SESSION_H_
