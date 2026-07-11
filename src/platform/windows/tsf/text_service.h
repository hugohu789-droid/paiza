// Copyright 2026 Paiza. All rights reserved.
//
// The TSF text service (TIP). One instance is created per thread manager by
// the class factory. Responsibilities:
//   * key event sink: consume letters into the Latin key buffer
//   * composition: show auto-transliterated Mongolian while typing
//   * candidate window: populate from EngineProxy, commit on digit/space
//   * display attribute provider: underline the composition

#ifndef PAIZA_TSF_TEXT_SERVICE_H_
#define PAIZA_TSF_TEXT_SERVICE_H_

#include <msctf.h>
#include <windows.h>
#include <wrl/client.h>

#include <memory>
#include <string>
#include <vector>

#include "core/candidate.h"
#include "core/fixed_sequences.h"
#include "core/key_mapping_store.h"
#include "tsf/engine_proxy.h"
#include "ui/candidate_window.h"

namespace paiza::tsf {

class TextService final : public ITfTextInputProcessorEx,
                          public ITfThreadMgrEventSink,
                          public ITfKeyEventSink,
                          public ITfCompositionSink,
                          public ITfDisplayAttributeProvider {
 public:
  TextService();
  TextService(const TextService&) = delete;
  TextService& operator=(const TextService&) = delete;

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // ITfTextInputProcessor
  STDMETHODIMP Activate(ITfThreadMgr* thread_mgr,
                        TfClientId client_id) override;
  STDMETHODIMP Deactivate() override;
  // ITfTextInputProcessorEx
  STDMETHODIMP ActivateEx(ITfThreadMgr* thread_mgr, TfClientId client_id,
                          DWORD flags) override;

  // ITfThreadMgrEventSink
  STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* doc_mgr) override;
  STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* doc_mgr) override;
  STDMETHODIMP OnSetFocus(ITfDocumentMgr* focus,
                          ITfDocumentMgr* prev_focus) override;
  STDMETHODIMP OnPushContext(ITfContext* context) override;
  STDMETHODIMP OnPopContext(ITfContext* context) override;

  // ITfKeyEventSink
  STDMETHODIMP OnSetFocus(BOOL foreground) override;
  STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wparam,
                             LPARAM lparam, BOOL* eaten) override;
  STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
                         BOOL* eaten) override;
  STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                           BOOL* eaten) override;
  STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                       BOOL* eaten) override;
  STDMETHODIMP OnPreservedKey(ITfContext* context, REFGUID guid,
                              BOOL* eaten) override;

  // ITfCompositionSink
  STDMETHODIMP OnCompositionTerminated(TfEditCookie ec_write,
                                       ITfComposition* composition) override;

  // ITfDisplayAttributeProvider
  STDMETHODIMP EnumDisplayAttributeInfo(
      IEnumTfDisplayAttributeInfo** enum_info) override;
  STDMETHODIMP GetDisplayAttributeInfo(
      REFGUID guid, ITfDisplayAttributeInfo** info) override;

 private:
  ~TextService();

  // Key handling (key_event_sink.cc) -------------------------------------
  // Central decision point shared by OnTestKeyDown/OnKeyDown. When
  // `apply` is false only the would-eat decision is computed.
  HRESULT HandleKeyDown(ITfContext* context, WPARAM wparam, bool apply,
                        BOOL* eaten);
  bool IsComposing() const { return composition_ != nullptr; }

  // Composition plumbing (composition_controller.cc) ----------------------
  HRESULT StartComposition(ITfContext* context);
  HRESULT UpdateComposition(const std::wstring& display_text);
  HRESULT CommitText(const std::wstring& text);
  HRESULT CancelComposition();
  // Applies (or clears) the input display attribute on `range`.
  void ApplyDisplayAttribute(TfEditCookie ec, ITfContext* context,
                             ITfRange* range, bool apply);
  // Repositions the candidate window next to the composition (uses
  // GetTextExt; falls back to the mouse cursor position).
  void UpdateCandidateWindowPosition(TfEditCookie ec, ITfContext* context);
  void ResetCompositionState();

  // Candidate flow ---------------------------------------------------------
  void RefreshCandidates();
  void CommitCandidateByIndex(size_t index);
  ui::CandidateWindow* EnsureCandidateWindow();
  // Converts the Latin buffer with the current (hot-reloadable, user
  // editable) key mapping.
  std::wstring ConvertBuffer();

  // State -------------------------------------------------------------------
  LONG ref_count_ = 1;
  Microsoft::WRL::ComPtr<ITfThreadMgr> thread_mgr_;
  TfClientId client_id_ = TF_CLIENTID_NULL;
  DWORD thread_mgr_sink_cookie_ = TF_INVALID_COOKIE;
  TfGuidAtom input_attribute_atom_ = TF_INVALID_GUIDATOM;

  Microsoft::WRL::ComPtr<ITfComposition> composition_;
  Microsoft::WRL::ComPtr<ITfContext> active_context_;
  std::string latin_buffer_;
  std::vector<Candidate> candidates_;

  std::unique_ptr<ui::CandidateWindow> candidate_window_;
  EngineProxy engine_;
  KeyMappingStore mapping_store_;
  FixedSequenceTable fixed_sequences_;  // Compose-side MVS/harmony (GB/T D.2).
};

}  // namespace paiza::tsf

#endif  // PAIZA_TSF_TEXT_SERVICE_H_
