// Copyright 2026 Paiza. All rights reserved.
//
// Composition-related member functions of TextService: starting/updating/
// committing the composition and keeping the candidate window anchored to
// the composition text.

#include <msctf.h>

#include "common/logging.h"
#include "tsf/edit_session.h"
#include "tsf/text_service.h"
#include "ui/dpi.h"

using Microsoft::WRL::ComPtr;

namespace paiza::tsf {

HRESULT TextService::StartComposition(ITfContext* context) {
  if (context == nullptr) return E_INVALIDARG;
  if (IsComposing()) return S_OK;
  active_context_ = context;

  return RequestEditSession(
      context, client_id_, TF_ES_READWRITE | TF_ES_SYNC,
      [this, context](TfEditCookie ec) -> HRESULT {
        ComPtr<ITfInsertAtSelection> insert_at_selection;
        HRESULT hr = context->QueryInterface(
            IID_PPV_ARGS(insert_at_selection.GetAddressOf()));
        if (FAILED(hr)) return hr;

        ComPtr<ITfRange> range;
        hr = insert_at_selection->InsertTextAtSelection(
            ec, TF_IAS_QUERYONLY, nullptr, 0, range.GetAddressOf());
        if (FAILED(hr) || range == nullptr) return hr;

        ComPtr<ITfContextComposition> context_composition;
        hr = context->QueryInterface(
            IID_PPV_ARGS(context_composition.GetAddressOf()));
        if (FAILED(hr)) return hr;

        ComPtr<ITfComposition> composition;
        hr = context_composition->StartComposition(
            ec, range.Get(), static_cast<ITfCompositionSink*>(this),
            composition.GetAddressOf());
        if (FAILED(hr) || composition == nullptr) {
          return FAILED(hr) ? hr : E_FAIL;
        }
        composition_ = composition;

        // Collapse the selection to the end of the (empty) composition.
        TF_SELECTION selection = {};
        selection.range = range.Get();
        selection.style.ase = TF_AE_NONE;
        selection.style.fInterimChar = FALSE;
        range->Collapse(ec, TF_ANCHOR_END);
        context->SetSelection(ec, 1, &selection);
        return S_OK;
      });
}

HRESULT TextService::UpdateComposition(const std::wstring& display_text) {
  if (!IsComposing() || active_context_ == nullptr) return E_UNEXPECTED;
  ITfContext* context = active_context_.Get();

  return RequestEditSession(
      context, client_id_, TF_ES_READWRITE | TF_ES_SYNC,
      [this, context, &display_text](TfEditCookie ec) -> HRESULT {
        ComPtr<ITfRange> range;
        HRESULT hr = composition_->GetRange(range.GetAddressOf());
        if (FAILED(hr) || range == nullptr) return hr;

        hr = range->SetText(ec, 0, display_text.c_str(),
                            static_cast<LONG>(display_text.size()));
        if (FAILED(hr)) return hr;

        ApplyDisplayAttribute(ec, context, range.Get(), true);

        // Keep the caret at the end of the composition.
        ComPtr<ITfRange> end_range;
        if (SUCCEEDED(range->Clone(end_range.GetAddressOf()))) {
          end_range->Collapse(ec, TF_ANCHOR_END);
          TF_SELECTION selection = {};
          selection.range = end_range.Get();
          selection.style.ase = TF_AE_NONE;
          selection.style.fInterimChar = FALSE;
          context->SetSelection(ec, 1, &selection);
        }

        UpdateCandidateWindowPosition(ec, context);
        return S_OK;
      });
}

HRESULT TextService::CommitText(const std::wstring& text) {
  if (!IsComposing() || active_context_ == nullptr) return E_UNEXPECTED;
  ITfContext* context = active_context_.Get();

  // Feed the service-side dynamic user lexicon (fire-and-forget) while
  // the typed buffer is still alive; the edit session below clears it.
  engine_.NotifyCommit(latin_buffer_, text);

  // TF_ES_ASYNCDONTCARE: commits may originate from mouse clicks on the
  // candidate window (outside a TSF callback) where synchronous sessions
  // are not granted.
  const HRESULT hr = RequestEditSession(
      context, client_id_, TF_ES_READWRITE | TF_ES_ASYNCDONTCARE,
      [this, context, text](TfEditCookie ec) -> HRESULT {
        ComPtr<ITfRange> range;
        HRESULT session_hr = composition_->GetRange(range.GetAddressOf());
        if (SUCCEEDED(session_hr) && range != nullptr) {
          range->SetText(ec, 0, text.c_str(),
                         static_cast<LONG>(text.size()));
          // Clear the input attribute from the committed text.
          ApplyDisplayAttribute(ec, context, range.Get(), false);
          range->Collapse(ec, TF_ANCHOR_END);
          TF_SELECTION selection = {};
          selection.range = range.Get();
          selection.style.ase = TF_AE_NONE;
          selection.style.fInterimChar = FALSE;
          context->SetSelection(ec, 1, &selection);
        }
        composition_->EndComposition(ec);
        ResetCompositionState();
        return S_OK;
      });
  return hr;
}

HRESULT TextService::CancelComposition() {
  if (!IsComposing() || active_context_ == nullptr) {
    ResetCompositionState();
    return S_OK;
  }
  ITfContext* context = active_context_.Get();
  return RequestEditSession(
      context, client_id_, TF_ES_READWRITE | TF_ES_ASYNCDONTCARE,
      [this, context](TfEditCookie ec) -> HRESULT {
        ComPtr<ITfRange> range;
        if (SUCCEEDED(composition_->GetRange(range.GetAddressOf())) &&
            range != nullptr) {
          range->SetText(ec, 0, L"", 0);
          ApplyDisplayAttribute(ec, context, range.Get(), false);
        }
        composition_->EndComposition(ec);
        ResetCompositionState();
        return S_OK;
      });
}

void TextService::ApplyDisplayAttribute(TfEditCookie ec, ITfContext* context,
                                        ITfRange* range, bool apply) {
  if (input_attribute_atom_ == TF_INVALID_GUIDATOM) return;
  ComPtr<ITfProperty> property;
  if (FAILED(context->GetProperty(GUID_PROP_ATTRIBUTE,
                                  property.GetAddressOf()))) {
    return;
  }
  if (apply) {
    VARIANT var;
    ::VariantInit(&var);
    var.vt = VT_I4;
    var.lVal = static_cast<LONG>(input_attribute_atom_);
    property->SetValue(ec, range, &var);
  } else {
    property->Clear(ec, range);
  }
}

void TextService::UpdateCandidateWindowPosition(TfEditCookie ec,
                                                ITfContext* context) {
  if (candidate_window_ == nullptr || !IsComposing()) return;

  RECT text_rect = {};
  bool have_rect = false;

  ComPtr<ITfContextView> view;
  if (SUCCEEDED(context->GetActiveView(view.GetAddressOf())) &&
      view != nullptr) {
    ComPtr<ITfRange> range;
    if (SUCCEEDED(composition_->GetRange(range.GetAddressOf())) &&
        range != nullptr) {
      BOOL clipped = FALSE;
      if (SUCCEEDED(view->GetTextExt(ec, range.Get(), &text_rect,
                                     &clipped)) &&
          (text_rect.right > text_rect.left ||
           text_rect.bottom > text_rect.top)) {
        have_rect = true;
        // GetTextExt reports coordinates in the host's (possibly
        // DPI-virtualized) space; our popup positions in physical pixels.
        // The conversion is an identity for DPI-aware hosts.
        HWND view_hwnd = nullptr;
        if (SUCCEEDED(view->GetWnd(&view_hwnd)) && view_hwnd != nullptr) {
          POINT top_left = {text_rect.left, text_rect.top};
          POINT bottom_right = {text_rect.right, text_rect.bottom};
          ::LogicalToPhysicalPointForPerMonitorDPI(view_hwnd, &top_left);
          ::LogicalToPhysicalPointForPerMonitorDPI(view_hwnd,
                                                   &bottom_right);
          text_rect = {top_left.x, top_left.y, bottom_right.x,
                       bottom_right.y};
        }
      }
    }
  }
  if (!have_rect) {
    // Fallback: anchor at the mouse cursor (spec item 6 allows tracking
    // the pointer when the exact text position is unavailable). Query the
    // cursor in physical pixels regardless of the host's DPI context.
    ui::ScopedPerMonitorDpi dpi_scope;
    POINT cursor = {};
    ::GetCursorPos(&cursor);
    text_rect = {cursor.x, cursor.y, cursor.x, cursor.y + 20};
  }
  candidate_window_->MoveNearCaret(text_rect);
}

void TextService::ResetCompositionState() {
  composition_.Reset();
  active_context_.Reset();
  latin_buffer_.clear();
  candidates_.clear();
  if (candidate_window_ != nullptr) candidate_window_->Hide();
}

}  // namespace paiza::tsf
