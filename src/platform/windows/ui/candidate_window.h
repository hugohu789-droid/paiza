// Copyright 2026 Paiza. All rights reserved.
//
// The IME candidate popup: a CandidateListView plus paging buttons and a
// page indicator in the bottom-right corner (spec item 6). Positioned by
// the TSF layer next to the composition caret and never activated.

#ifndef PAIZA_UI_CANDIDATE_WINDOW_H_
#define PAIZA_UI_CANDIDATE_WINDOW_H_

#include <functional>
#include <vector>

#include "core/candidate.h"
#include "ui/button.h"
#include "ui/candidate_list_view.h"
#include "ui/label.h"
#include "ui/window.h"

namespace paiza::ui {

class CandidateWindow : public Window {
 public:
  CandidateWindow() = default;

  // Creates the (hidden) window if needed. Safe to call repeatedly.
  bool EnsureCreated();

  // Content ----------------------------------------------------------------
  void SetCandidates(std::vector<Candidate> candidates);
  bool HasCandidates() const;
  // Preedit strip: shows the composition string inside the popup with the
  // user's Mongolian font and correct shaping. The inline composition is
  // rendered by the HOST application with the host's own font (TSF offers
  // no way to set it), so hosts without a Mongolian font show isolated
  // letter forms there — this strip is the reliable preview.
  void SetPreedit(const std::wstring& preedit);

  // Keyboard plumbing from the TSF layer ------------------------------------
  bool SelectByDigit(int digit, size_t* chosen_index);
  void MoveSelection(int delta);  // +1 / -1
  void PageForward();
  void PageBackward();
  size_t selected_index() const;
  const Candidate* selected_candidate() const;

  // Positions the window right of the caret rectangle (screen coordinates),
  // clamped to the monitor work area. Vertical Mongolian text advances in
  // columns to the right, so right-of-caret is the natural spot.
  void MoveNearCaret(const RECT& caret_screen_rect);

  // Fired on mouse click or (via SelectByDigit) digit selection.
  void SetOnCandidateChosen(std::function<void(size_t)> callback) {
    on_chosen_ = std::move(callback);
  }

  // Applies the user-selected Mongolian font (no-op if unchanged).
  void SetFontFamily(const std::wstring& font_family);

  // Debug aid: overlays a vertical reference line on each candidate column
  // to inspect stem/NIRUGU alignment.
  void SetDebugSpine(bool on) {
    if (EnsureCreated()) list_->set_debug_spine(on);
  }

 protected:
  uint32_t background_color() const override { return 0xFFFFFFFF; }
  void OnPaintBackground(RenderContext& context) override;
  void OnPaintOverlay(RenderContext& context) override;
  void OnDpiChanged() override;

 private:
  // Resizes the window to the measured content of the current page and
  // repositions the footer controls. Called on content and page changes.
  void Relayout();
  void UpdatePageLabel();

  CandidateListView* list_ = nullptr;
  Label* preedit_label_ = nullptr;
  Button* prev_button_ = nullptr;
  Button* next_button_ = nullptr;
  Label* page_label_ = nullptr;
  bool has_footer_ = false;
  // Logical x of the hairline between the preedit strip and the list
  // (0 = no preedit shown).
  int preedit_separator_x_ = 0;
  std::function<void(size_t)> on_chosen_;
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_CANDIDATE_WINDOW_H_
