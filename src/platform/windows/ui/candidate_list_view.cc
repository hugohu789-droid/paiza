// Copyright 2026 Paiza. All rights reserved.

#include "ui/candidate_list_view.h"

#include <algorithm>

namespace paiza::ui {
namespace {

// Refined, low-contrast palette for a compact, premium look.
constexpr uint32_t kColorNumber = 0xFFAAB0B8;          // Muted grey.
constexpr uint32_t kColorNumberSelected = 0xFF2563EB;  // Accent (indigo).
constexpr uint32_t kColorText = 0xFF1F2430;            // Near-black ink.
constexpr uint32_t kColorTextSelected = 0xFF16305C;    // Deep accent ink.
constexpr uint32_t kColorSelectedBg = 0xFFEAF1FE;      // Soft accent fill.
constexpr uint32_t kColorHoverBg = 0xFFF3F5F8;         // Faint grey fill.

constexpr float kCandidateFontSize = 16.0f;
constexpr float kNumberFontSize = 9.5f;

TextStyle NumberTextStyle() {
  TextStyle style;
  style.font_family = L"Segoe UI";
  style.font_size = kNumberFontSize;
  style.color = kColorNumber;
  style.center_horizontal = true;
  style.center_vertical = true;
  return style;
}

}  // namespace

TextStyle CandidateListView::CandidateTextStyle() const {
  TextStyle style;
  style.font_family = font_family_;
  style.font_size = kCandidateFontSize;
  style.color = kColorText;
  style.orientation = TextOrientation::kVertical;
  style.center_horizontal = true;
  return style;
}

void CandidateListView::set_font_family(std::wstring font_family) {
  if (font_family.empty() || font_family_ == font_family) return;
  font_family_ = std::move(font_family);
  RecomputeMetrics();
  Invalidate();
}

void CandidateListView::SetCandidates(std::vector<Candidate> candidates) {
  candidates_ = std::move(candidates);
  current_page_ = 0;
  selected_index_ = 0;
  hovered_index_ = SIZE_MAX;
  RecomputeMetrics();
  Invalidate();
}

void CandidateListView::Clear() { SetCandidates({}); }

void CandidateListView::set_page_size(size_t page_size) {
  page_size_ = page_size == 0 ? 1 : page_size;
  current_page_ = selected_index_ / page_size_;
  RecomputeMetrics();
}

size_t CandidateListView::page_count() const {
  if (candidates_.empty()) return 0;
  return (candidates_.size() + page_size_ - 1) / page_size_;
}

void CandidateListView::PageRange(size_t* first, size_t* last) const {
  *first = current_page_ * page_size_;
  *last = std::min(*first + page_size_, candidates_.size());
}

void CandidateListView::RecomputeMetrics() {
  size_t first = 0;
  size_t last = 0;
  PageRange(&first, &last);

  // Measure every candidate on the current page with the same style used
  // for painting, so the window is always big enough (fix: long vertical
  // words used to be clipped by the fixed 150 px text band).
  const TextStyle style = CandidateTextStyle();
  column_widths_.clear();
  int max_height = 48;
  for (size_t i = first; i < last; ++i) {
    const Size size = RenderContext::MeasureText(
        candidates_[i].text, style, {200, kMaxTextBandHeight});
    // Each column hugs its own glyph width with slim side padding, so the
    // window stays as tight as the content instead of padding every column
    // out to the widest word.
    column_widths_.push_back(
        std::max(kMinColumnWidth, size.width + 2 * kColumnSidePadding));
    max_height = std::max(max_height, size.height);
  }
  text_band_height_ = std::min(max_height + kTextBandPadding,
                               static_cast<int>(kMaxTextBandHeight));
}

int CandidateListView::ColumnOffset(size_t col) const {
  int offset = 0;
  for (size_t i = 0; i < col && i < column_widths_.size(); ++i) {
    offset += column_widths_[i];
  }
  return offset;
}

bool CandidateListView::NextPage() {
  if (current_page_ + 1 >= page_count()) return false;
  ++current_page_;
  selected_index_ = current_page_ * page_size_;
  RecomputeMetrics();
  Invalidate();
  return true;
}

bool CandidateListView::PrevPage() {
  if (current_page_ == 0) return false;
  --current_page_;
  selected_index_ = current_page_ * page_size_;
  RecomputeMetrics();
  Invalidate();
  return true;
}

void CandidateListView::SelectNext() {
  if (candidates_.empty()) return;
  selected_index_ = (selected_index_ + 1) % candidates_.size();
  const size_t page = selected_index_ / page_size_;
  if (page != current_page_) {
    current_page_ = page;
    RecomputeMetrics();
  }
  Invalidate();
}

void CandidateListView::SelectPrev() {
  if (candidates_.empty()) return;
  selected_index_ =
      (selected_index_ + candidates_.size() - 1) % candidates_.size();
  const size_t page = selected_index_ / page_size_;
  if (page != current_page_) {
    current_page_ = page;
    RecomputeMetrics();
  }
  Invalidate();
}

bool CandidateListView::DigitToIndex(int digit, size_t* index) const {
  if (digit < 1) return false;
  size_t first = 0;
  size_t last = 0;
  PageRange(&first, &last);
  const size_t candidate = first + static_cast<size_t>(digit - 1);
  if (candidate >= last) return false;
  *index = candidate;
  return true;
}

Size CandidateListView::MeasureContent() const {
  int total_width = 0;
  for (const int w : column_widths_) total_width += w;
  return {std::max(total_width, kMinColumnWidth),
          kNumberBandHeight + text_band_height_};
}

bool CandidateListView::HitCandidate(Point p, size_t* index) const {
  if (!bounds().Contains(p)) return false;
  size_t first = 0;
  size_t last = 0;
  PageRange(&first, &last);
  int x = bounds().x;
  for (size_t col = 0; col < column_widths_.size(); ++col) {
    if (p.x >= x && p.x < x + column_widths_[col]) {
      const size_t candidate = first + col;
      if (candidate >= last) return false;
      *index = candidate;
      return true;
    }
    x += column_widths_[col];
  }
  return false;
}

void CandidateListView::OnPaint(RenderContext& context) {
  size_t first = 0;
  size_t last = 0;
  PageRange(&first, &last);

  TextStyle number_style = NumberTextStyle();
  TextStyle text_style = CandidateTextStyle();

  for (size_t i = first; i < last; ++i) {
    const size_t column = i - first;
    if (column >= column_widths_.size()) break;
    const int column_width = column_widths_[column];
    const Rect column_rect = {bounds().x + ColumnOffset(column),
                              bounds().y, column_width,
                              kNumberBandHeight + text_band_height_};
    const bool selected = i == selected_index_;

    // A slim rounded pill hugs the column; the accent tint reads as a
    // premium highlight rather than a heavy block.
    if (selected) {
      context.FillRoundedRect(column_rect.Inflated(-3, -1), 7.0f,
                              kColorSelectedBg);
    } else if (i == hovered_index_) {
      context.FillRoundedRect(column_rect.Inflated(-3, -1), 7.0f,
                              kColorHoverBg);
    }

    // Number above the candidate text (spec item 6); accent + bold when
    // selected to anchor the eye.
    number_style.color = selected ? kColorNumberSelected : kColorNumber;
    number_style.bold = selected;
    const Rect number_rect = {column_rect.x, column_rect.y, column_width,
                              kNumberBandHeight};
    context.DrawTextBlock(std::to_wstring(i - first + 1), number_rect,
                          number_style);

    text_style.color = selected ? kColorTextSelected : kColorText;
    const Rect text_rect = {column_rect.x,
                            column_rect.y + kNumberBandHeight,
                            column_width, text_band_height_};
    context.DrawTextBlock(candidates_[i].text, text_rect, text_style);

    // Debug: a thin reference line down the column centre, to check that
    // each character's stem/NIRUGU lands on (or a constant distance from)
    // the same vertical line.
    if (debug_spine_) {
      const int cx = column_rect.x + column_width / 2;
      context.DrawLine({cx, text_rect.y}, {cx, text_rect.bottom()},
                       0xC0E01515, 1.0f);
    }
  }
}

void CandidateListView::HandleEvent(Event& event) {
  auto* mouse = dynamic_cast<MouseEvent*>(&event);
  if (mouse == nullptr) return;
  switch (event.type()) {
    case EventType::kMouseMove: {
      size_t index = SIZE_MAX;
      HitCandidate(mouse->position(), &index);
      if (index != hovered_index_) {
        hovered_index_ = index;
        Invalidate();
      }
      break;
    }
    case EventType::kMouseLeave:
      if (hovered_index_ != SIZE_MAX) {
        hovered_index_ = SIZE_MAX;
        Invalidate();
      }
      break;
    case EventType::kClick: {
      size_t index = 0;
      if (HitCandidate(mouse->position(), &index)) {
        event.set_handled();
        if (on_choose_) on_choose_(index);
      }
      break;
    }
    default:
      break;
  }
}

}  // namespace paiza::ui
