// Copyright 2026 Paiza. All rights reserved.

#include "tsf/text_service.h"

#include <olectl.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "common/logging.h"
#include "core/romanization.h"
#include "tsf/guids.h"
#include "tsf/module.h"

using Microsoft::WRL::ComPtr;

namespace paiza::tsf {
namespace {

// ---------------------------------------------------------------------------
// Display attribute plumbing: one attribute describing the composition
// (dotted underline), exposed through ITfDisplayAttributeProvider.

class DisplayAttributeInfo final : public ITfDisplayAttributeInfo {
 public:
  DisplayAttributeInfo() { ModuleAddRef(); }
  DisplayAttributeInfo(const DisplayAttributeInfo&) = delete;
  DisplayAttributeInfo& operator=(const DisplayAttributeInfo&) = delete;

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
    if (object == nullptr) return E_INVALIDARG;
    if (::IsEqualIID(riid, IID_IUnknown) ||
        ::IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) {
      *object = static_cast<ITfDisplayAttributeInfo*>(this);
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

  // ITfDisplayAttributeInfo
  STDMETHODIMP GetGUID(GUID* guid) override {
    if (guid == nullptr) return E_INVALIDARG;
    *guid = kGuidDisplayAttributeInput;
    return S_OK;
  }
  STDMETHODIMP GetDescription(BSTR* description) override {
    if (description == nullptr) return E_INVALIDARG;
    *description = ::SysAllocString(L"Paiza composition input");
    return *description != nullptr ? S_OK : E_OUTOFMEMORY;
  }
  STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* attribute) override {
    if (attribute == nullptr) return E_INVALIDARG;
    *attribute = {};
    attribute->crText.type = TF_CT_NONE;
    attribute->crBk.type = TF_CT_NONE;
    attribute->crLine.type = TF_CT_NONE;
    attribute->lsStyle = TF_LS_DOT;
    attribute->fBoldLine = FALSE;
    attribute->bAttr = TF_ATTR_INPUT;
    return S_OK;
  }
  STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP Reset() override { return S_OK; }

 private:
  ~DisplayAttributeInfo() { ModuleRelease(); }
  LONG ref_count_ = 1;
};

class EnumDisplayAttributeInfoImpl final
    : public IEnumTfDisplayAttributeInfo {
 public:
  EnumDisplayAttributeInfoImpl() { ModuleAddRef(); }
  EnumDisplayAttributeInfoImpl(const EnumDisplayAttributeInfoImpl&) = delete;
  EnumDisplayAttributeInfoImpl& operator=(
      const EnumDisplayAttributeInfoImpl&) = delete;

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
    if (object == nullptr) return E_INVALIDARG;
    if (::IsEqualIID(riid, IID_IUnknown) ||
        ::IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo)) {
      *object = static_cast<IEnumTfDisplayAttributeInfo*>(this);
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

  // IEnumTfDisplayAttributeInfo
  STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** clone) override {
    if (clone == nullptr) return E_INVALIDARG;
    auto* copy = new EnumDisplayAttributeInfoImpl();
    copy->index_ = index_;
    *clone = copy;
    return S_OK;
  }
  STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo** info,
                    ULONG* fetched) override {
    ULONG produced = 0;
    if (info == nullptr) return E_INVALIDARG;
    if (count > 0 && index_ == 0) {
      info[0] = new DisplayAttributeInfo();
      index_ = 1;
      produced = 1;
    }
    if (fetched != nullptr) *fetched = produced;
    return produced == count ? S_OK : S_FALSE;
  }
  STDMETHODIMP Reset() override {
    index_ = 0;
    return S_OK;
  }
  STDMETHODIMP Skip(ULONG count) override {
    index_ += count;
    return index_ <= 1 ? S_OK : S_FALSE;
  }

 private:
  ~EnumDisplayAttributeInfoImpl() { ModuleRelease(); }
  LONG ref_count_ = 1;
  ULONG index_ = 0;
};

}  // namespace

// ---------------------------------------------------------------------------
// Construction / IUnknown

TextService::TextService() {
  ModuleAddRef();
  // Compose-side suffix rules (GB/T D.2), deployed next to the module.
  std::ifstream file(std::filesystem::path(ModuleDirectory()) /
                         L"fixed_sequences.txt",
                     std::ios::binary);
  if (file.is_open()) {
    std::ostringstream buffer;
    buffer << file.rdbuf();
    fixed_sequences_.LoadFromString(buffer.str());
  }
}

TextService::~TextService() { ModuleRelease(); }

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** object) {
  if (object == nullptr) return E_INVALIDARG;
  *object = nullptr;
  if (::IsEqualIID(riid, IID_IUnknown) ||
      ::IsEqualIID(riid, IID_ITfTextInputProcessor)) {
    *object = static_cast<ITfTextInputProcessor*>(this);
  } else if (::IsEqualIID(riid, IID_ITfTextInputProcessorEx)) {
    *object = static_cast<ITfTextInputProcessorEx*>(this);
  } else if (::IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
    *object = static_cast<ITfThreadMgrEventSink*>(this);
  } else if (::IsEqualIID(riid, IID_ITfKeyEventSink)) {
    *object = static_cast<ITfKeyEventSink*>(this);
  } else if (::IsEqualIID(riid, IID_ITfCompositionSink)) {
    *object = static_cast<ITfCompositionSink*>(this);
  } else if (::IsEqualIID(riid, IID_ITfDisplayAttributeProvider)) {
    *object = static_cast<ITfDisplayAttributeProvider*>(this);
  }
  if (*object == nullptr) return E_NOINTERFACE;
  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() {
  return ::InterlockedIncrement(&ref_count_);
}

STDMETHODIMP_(ULONG) TextService::Release() {
  const ULONG count = ::InterlockedDecrement(&ref_count_);
  if (count == 0) delete this;
  return count;
}

// ---------------------------------------------------------------------------
// Activation

STDMETHODIMP TextService::Activate(ITfThreadMgr* thread_mgr,
                                   TfClientId client_id) {
  return ActivateEx(thread_mgr, client_id, 0);
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* thread_mgr,
                                     TfClientId client_id, DWORD flags) {
  (void)flags;
  if (thread_mgr == nullptr) return E_INVALIDARG;
  thread_mgr_ = thread_mgr;
  client_id_ = client_id;

  // Thread manager events (focus changes etc.).
  ComPtr<ITfSource> source;
  if (SUCCEEDED(thread_mgr_.As(&source))) {
    source->AdviseSink(IID_ITfThreadMgrEventSink,
                       static_cast<ITfThreadMgrEventSink*>(this),
                       &thread_mgr_sink_cookie_);
  }

  // Keyboard events.
  ComPtr<ITfKeystrokeMgr> keystroke_mgr;
  if (SUCCEEDED(thread_mgr_.As(&keystroke_mgr))) {
    keystroke_mgr->AdviseKeyEventSink(
        client_id_, static_cast<ITfKeyEventSink*>(this), TRUE);
  }

  // Register the display attribute GUID to get its atom.
  ComPtr<ITfCategoryMgr> category_mgr;
  if (SUCCEEDED(::CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&category_mgr)))) {
    category_mgr->RegisterGUID(kGuidDisplayAttributeInput,
                               &input_attribute_atom_);
  }

  PAIZA_LOG_INFO(L"TextService activated");
  return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
  CancelComposition();
  if (candidate_window_ != nullptr) {
    candidate_window_->Destroy();
    candidate_window_.reset();
  }

  if (thread_mgr_ != nullptr) {
    ComPtr<ITfSource> source;
    if (thread_mgr_sink_cookie_ != TF_INVALID_COOKIE &&
        SUCCEEDED(thread_mgr_.As(&source))) {
      source->UnadviseSink(thread_mgr_sink_cookie_);
    }
    ComPtr<ITfKeystrokeMgr> keystroke_mgr;
    if (SUCCEEDED(thread_mgr_.As(&keystroke_mgr))) {
      keystroke_mgr->UnadviseKeyEventSink(client_id_);
    }
  }
  thread_mgr_sink_cookie_ = TF_INVALID_COOKIE;
  thread_mgr_.Reset();
  client_id_ = TF_CLIENTID_NULL;
  PAIZA_LOG_INFO(L"TextService deactivated");
  return S_OK;
}

// ---------------------------------------------------------------------------
// ITfThreadMgrEventSink

STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr*) { return S_OK; }

STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr*) {
  return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* focus,
                                     ITfDocumentMgr* prev_focus) {
  (void)focus;
  (void)prev_focus;
  // Focus moved to another document: the composition (if any) will be
  // terminated by TSF; make sure the popup does not linger.
  if (candidate_window_ != nullptr) candidate_window_->Hide();
  return S_OK;
}

STDMETHODIMP TextService::OnPushContext(ITfContext*) { return S_OK; }

STDMETHODIMP TextService::OnPopContext(ITfContext*) { return S_OK; }

// ---------------------------------------------------------------------------
// ITfCompositionSink

STDMETHODIMP TextService::OnCompositionTerminated(
    TfEditCookie ec_write, ITfComposition* composition) {
  (void)ec_write;
  (void)composition;
  // The host (or a focus change) ended the composition for us.
  ResetCompositionState();
  return S_OK;
}

// ---------------------------------------------------------------------------
// ITfDisplayAttributeProvider

STDMETHODIMP TextService::EnumDisplayAttributeInfo(
    IEnumTfDisplayAttributeInfo** enum_info) {
  if (enum_info == nullptr) return E_INVALIDARG;
  *enum_info = new EnumDisplayAttributeInfoImpl();
  return S_OK;
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(
    REFGUID guid, ITfDisplayAttributeInfo** info) {
  if (info == nullptr) return E_INVALIDARG;
  *info = nullptr;
  if (!::IsEqualGUID(guid, kGuidDisplayAttributeInput)) return E_INVALIDARG;
  *info = new DisplayAttributeInfo();
  return S_OK;
}

// ---------------------------------------------------------------------------
// Candidate flow

namespace {

// Reads the user's candidate font (written by the service's settings
// dialog, see service/settings_store.cc). Read per refresh so a saved
// change applies on the next keystroke; falls back to the default when the
// value is missing or unreadable (e.g. AppContainer hosts).
std::wstring ReadCandidateFontSetting() {
  wchar_t buffer[256] = {};
  DWORD size = sizeof(buffer) - sizeof(wchar_t);
  if (::RegGetValueW(HKEY_CURRENT_USER, L"Software\\PaizaIME",
                     L"CandidateFont", RRF_RT_REG_SZ, nullptr, buffer,
                     &size) == ERROR_SUCCESS &&
      buffer[0] != L'\0') {
    return buffer;
  }
  return L"Noto Sans Mongolian";
}

}  // namespace

ui::CandidateWindow* TextService::EnsureCandidateWindow() {
  if (candidate_window_ == nullptr) {
    candidate_window_ = std::make_unique<ui::CandidateWindow>();
    candidate_window_->EnsureCreated();
    candidate_window_->SetOnCandidateChosen(
        [this](size_t index) { CommitCandidateByIndex(index); });
  }
  return candidate_window_.get();
}

std::wstring TextService::ConvertBuffer() {
  return fixed_sequences_.InsertSuffixMvs(
      mapping_store_.Get().Convert(latin_buffer_));
}

void TextService::RefreshCandidates() {
  const std::wstring converted = ConvertBuffer();
  // Dual query, identical to the cross-platform InputSession: the raw typed
  // latin (gloss / literal keys) plus the romanization of the preedit (the
  // layout-independent, digraph-aware key that is the same regardless of how
  // the user remapped the keyboard). They coincide for default-layout input.
  candidates_ = engine_.QueryCandidates(latin_buffer_, 27,  // 3 pages of 9.
                                        L"");
  const std::string canonical = Romanize(converted);
  if (!canonical.empty() && canonical != latin_buffer_) {
    const auto extra = engine_.QueryCandidates(canonical, 27, L"");
    for (const Candidate& c : extra) {
      const bool dup = std::any_of(
          candidates_.begin(), candidates_.end(),
          [&c](const Candidate& e) { return e.text == c.text; });
      if (!dup) candidates_.push_back(c);
    }
    std::stable_sort(candidates_.begin(), candidates_.end(),
                     [](const Candidate& a, const Candidate& b) {
                       return a.score > b.score;
                     });
    if (candidates_.size() > 27) candidates_.resize(27);
  }
  if (candidates_.empty() && !converted.empty()) {
    // Fallback so typing keeps working with the service down (mirrors
    // EngineProxy's own fallback, applied after the merge).
    Candidate fallback;
    fallback.text = converted;
    candidates_.push_back(std::move(fallback));
  }
  ui::CandidateWindow* window = EnsureCandidateWindow();
  window->SetFontFamily(ReadCandidateFontSetting());
  // The popup shows the preedit with the user's Mongolian font: the
  // inline composition is rendered by the HOST app with the host's own
  // font (TSF cannot set it) and may show isolated forms there.
  window->SetPreedit(converted);
  window->SetCandidates(candidates_);
}

void TextService::CommitCandidateByIndex(size_t index) {
  if (index >= candidates_.size()) return;
  // Mouse clicks arrive outside any TSF callback, so the edit session must
  // be asynchronous-capable; CommitText handles that.
  CommitText(candidates_[index].text);
}

}  // namespace paiza::tsf
