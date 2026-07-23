// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Paged candidate list widget. Candidates are laid out as vertical-text
// columns flowing left to right; each column shows its selection number
// above the Mongolian word (spec: numbers above the text, digit keys 1..9
// select on the current page). Column width and text height are measured
// from the actual candidates on the current page, so long words are never
// truncated.

#ifndef PAIZA_UI_CANDIDATE_LIST_VIEW_H_
#define PAIZA_UI_CANDIDATE_LIST_VIEW_H_

#include <functional>
#include <string>
#include <vector>

#include "core/candidate.h"
#include "ui/widget.h"

namespace paiza::ui {

class CandidateListView : public Widget {
 public:
  CandidateListView() = default;

  void SetCandidates(std::vector<Candidate> candidates);
  void Clear();

  // Paging ---------------------------------------------------------------
  size_t page_size() const { return page_size_; }
  void set_page_size(size_t page_size);
  size_t page_count() const;
  size_t current_page() const { return current_page_; }
  bool NextPage();
  bool PrevPage();

  // Selection ------------------------------------------------------------
  // Moves the highlight; wraps within the whole candidate list.
  void SelectNext();
  void SelectPrev();
  size_t selected_index() const { return selected_index_; }
  // Maps a digit key (1-based, on the current page) to a global candidate
  // index; returns false if the digit exceeds the page's candidate count.
  bool DigitToIndex(int digit, size_t* index) const;

  const std::vector<Candidate>& candidates() const { return candidates_; }
  bool empty() const { return candidates_.empty(); }

  // Invoked when the user clicks a candidate (argument: global index).
  void SetOnChoose(std::function<void(size_t)> on_choose) {
    on_choose_ = std::move(on_choose);
  }

  // Debug aid: draws a vertical reference line down each column centre so
  // the stem/NIRUGU alignment of every character can be eyeballed.
  void set_debug_spine(bool on) {
    debug_spine_ = on;
    Invalidate();
  }

  // Font used for the Mongolian candidate text (user setting). Caller is
  // responsible for re-running layout (metrics change with the font).
  void set_font_family(std::wstring font_family);
  const std::wstring& font_family() const { return font_family_; }

  // Layout metrics -------------------------------------------------------
  // Preferred size for the current page's content (measured from the
  // actual candidate texts; recomputed on candidate/page changes).
  Size MeasureContent() const;

  // Widget:
  void OnPaint(RenderContext& context) override;
  void HandleEvent(Event& event) override;

 private:
  static constexpr int kNumberBandHeight = 15;
  static constexpr int kMinColumnWidth = 18;
  static constexpr int kMaxTextBandHeight = 340;
  // Snug horizontal padding added around each vertical glyph so columns
  // hug the text instead of floating in whitespace.
  static constexpr int kColumnSidePadding = 2;
  static constexpr int kTextBandPadding = 4;

  // Candidates visible on the current page: [first, last).
  void PageRange(size_t* first, size_t* last) const;
  // Returns the global candidate index at a client point, or false.
  bool HitCandidate(Point p, size_t* index) const;
  // Re-measures per-column widths / text band height for the current page.
  void RecomputeMetrics();
  TextStyle CandidateTextStyle() const;
  // Local x offset (from bounds().x) of the column for page position `col`.
  int ColumnOffset(size_t col) const;

  std::vector<Candidate> candidates_;
  size_t page_size_ = 7;
  size_t current_page_ = 0;
  size_t selected_index_ = 0;
  size_t hovered_index_ = SIZE_MAX;
  // Per-column widths for the current page (each column hugs its own word,
  // so narrow candidates don't inherit the widest word's whitespace).
  std::vector<int> column_widths_;
  int text_band_height_ = 150;
  std::wstring font_family_ = L"Noto Sans Mongolian";
  bool debug_spine_ = false;
  std::function<void(size_t)> on_choose_;
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_CANDIDATE_LIST_VIEW_H_
