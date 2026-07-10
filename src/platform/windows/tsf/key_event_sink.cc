// Copyright 2026 Paiza. All rights reserved.
//
// ITfKeyEventSink implementation for TextService. Key policy:
//   a..z          -> append to the Latin buffer, refresh composition
//   Shift+a..z    -> upper-case key: the second (user-mappable) letter set
//   - _ @ # $ % * -> mapped punctuation (MVS, NNBSP, FVS1-4, BIRGA by
//                    default; any key present in keymap.txt composes)
//   1..9          -> commit that candidate on the current page (unshifted)
//   Space         -> commit the highlighted candidate (or the buffer)
//   Enter         -> commit the transliterated buffer as-is
//   Escape        -> cancel the composition
//   Backspace     -> remove the last letter
//   PgDn / PgUp   -> candidate paging
//   Arrows        -> move the candidate highlight
// Everything else while composing commits the buffer first, then the key
// goes through to the application.

#include <string>

#include "tsf/text_service.h"

namespace paiza::tsf {
namespace {

bool IsLetterKey(WPARAM wparam) { return wparam >= 'A' && wparam <= 'Z'; }

// Returns 1..9 for digit rows/numpad, 0 otherwise.
int DigitFromKey(WPARAM wparam) {
  if (wparam >= '1' && wparam <= '9') return static_cast<int>(wparam - '0');
  if (wparam >= VK_NUMPAD1 && wparam <= VK_NUMPAD9) {
    return static_cast<int>(wparam - VK_NUMPAD0);
  }
  return 0;
}

}  // namespace

STDMETHODIMP TextService::OnSetFocus(BOOL foreground) {
  if (!foreground && candidate_window_ != nullptr) {
    candidate_window_->Hide();
  }
  return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* context, WPARAM wparam,
                                        LPARAM lparam, BOOL* eaten) {
  (void)lparam;
  return HandleKeyDown(context, wparam, /*apply=*/false, eaten);
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wparam,
                                    LPARAM lparam, BOOL* eaten) {
  (void)lparam;
  return HandleKeyDown(context, wparam, /*apply=*/true, eaten);
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* context, WPARAM wparam,
                                      LPARAM lparam, BOOL* eaten) {
  (void)context;
  (void)wparam;
  (void)lparam;
  if (eaten == nullptr) return E_INVALIDARG;
  *eaten = FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* context, WPARAM wparam,
                                  LPARAM lparam, BOOL* eaten) {
  (void)context;
  (void)wparam;
  (void)lparam;
  if (eaten == nullptr) return E_INVALIDARG;
  *eaten = FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext* context, REFGUID guid,
                                         BOOL* eaten) {
  (void)context;
  (void)guid;
  if (eaten == nullptr) return E_INVALIDARG;
  *eaten = FALSE;
  return S_OK;
}

HRESULT TextService::HandleKeyDown(ITfContext* context, WPARAM wparam,
                                   bool apply, BOOL* eaten) {
  if (eaten == nullptr) return E_INVALIDARG;
  *eaten = FALSE;
  if (context == nullptr) return S_OK;

  // Modifier chords (Ctrl/Alt+key) belong to the application.
  if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
      (::GetKeyState(VK_MENU) & 0x8000) != 0) {
    return S_OK;
  }

  const bool composing = IsComposing();
  const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;

  // ---- composable characters: start/extend the composition ---------------
  // Letters (Shift XOR CapsLock selects the upper-case second letter
  // set), plus mapped punctuation: '-'/'_' on VK_OEM_MINUS and the
  // Shift-digit symbols @ # $ % * (FVS1-4, BIRGA by default; whatever the
  // user maps in keymap.txt participates automatically).
  char composable = 0;
  if (IsLetterKey(wparam)) {
    const bool caps = (::GetKeyState(VK_CAPITAL) & 1) != 0;
    const bool upper = shift != caps;
    composable = static_cast<char>((upper ? 'A' : 'a') + (wparam - 'A'));
  } else {
    char punctuation = 0;
    if (wparam == VK_OEM_MINUS) {
      punctuation = shift ? '_' : '-';
    } else if (shift && wparam >= '0' && wparam <= '9') {
      // US-layout Shift+digit symbols; only the mapped ones compose.
      constexpr char kShiftDigit[10] = {')', '!', '@', '#', '$',
                                        '%', '^', '&', '*', '('};
      punctuation = kShiftDigit[wparam - '0'];
    }
    if (punctuation != 0 &&
        !mapping_store_.Get().Lookup(std::string(1, punctuation)).empty()) {
      composable = punctuation;
    }
  }
  if (composable != 0) {
    *eaten = TRUE;
    if (!apply) return S_OK;

    if (!composing) {
      if (FAILED(StartComposition(context))) {
        *eaten = FALSE;
        return S_OK;
      }
    }
    latin_buffer_.push_back(composable);
    // The composition always shows the mapped conversion; the dictionary
    // candidates go to the popup (spec item 3).
    UpdateComposition(ConvertBuffer());
    RefreshCandidates();
    return S_OK;
  }

  if (!composing) return S_OK;  // Nothing below applies without composition.

  const bool has_candidates =
      candidate_window_ != nullptr && candidate_window_->HasCandidates();

  // ---- digit selection (unshifted digits only) ----------------------------
  const int digit = shift ? 0 : DigitFromKey(wparam);
  if (digit > 0 && has_candidates) {
    size_t index = 0;
    if (candidate_window_->SelectByDigit(digit, &index)) {
      *eaten = TRUE;
      if (apply) CommitCandidateByIndex(index);
    }
    return S_OK;
  }

  switch (wparam) {
    case VK_SPACE:
      *eaten = TRUE;
      if (apply) {
        const Candidate* selected =
            has_candidates ? candidate_window_->selected_candidate()
                           : nullptr;
        CommitText(selected != nullptr ? selected->text : ConvertBuffer());
      }
      return S_OK;
    case VK_RETURN:
      *eaten = TRUE;
      if (apply) CommitText(ConvertBuffer());
      return S_OK;
    case VK_ESCAPE:
      *eaten = TRUE;
      if (apply) CancelComposition();
      return S_OK;
    case VK_BACK:
      *eaten = TRUE;
      if (apply) {
        latin_buffer_.pop_back();
        if (latin_buffer_.empty()) {
          CancelComposition();
        } else {
          UpdateComposition(ConvertBuffer());
          RefreshCandidates();
        }
      }
      return S_OK;
    case VK_NEXT:
      *eaten = TRUE;
      if (apply && has_candidates) candidate_window_->PageForward();
      return S_OK;
    case VK_PRIOR:
      *eaten = TRUE;
      if (apply && has_candidates) candidate_window_->PageBackward();
      return S_OK;
    case VK_RIGHT:
    case VK_DOWN:
      *eaten = TRUE;
      if (apply && has_candidates) candidate_window_->MoveSelection(1);
      return S_OK;
    case VK_LEFT:
    case VK_UP:
      *eaten = TRUE;
      if (apply && has_candidates) candidate_window_->MoveSelection(-1);
      return S_OK;
    default:
      // Any other key: finish the composition with the current buffer and
      // let the key reach the application afterwards.
      if (apply) CommitText(ConvertBuffer());
      return S_OK;
  }
}

}  // namespace paiza::tsf
