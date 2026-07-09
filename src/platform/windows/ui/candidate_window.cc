// Copyright 2026 Paiza. All rights reserved.

#include "ui/candidate_window.h"

#include <algorithm>

#include "ui/dpi.h"

namespace paiza::ui {
namespace {

constexpr int kPadding = 6;
constexpr int kFooterHeight = 24;
constexpr int kPageButtonWidth = 22;
constexpr int kPageButtonHeight = 18;
constexpr int kPageLabelWidth = 40;
constexpr int kCaretGap = 4;

constexpr uint32_t kColorBorder = 0xFFE2E4E8;
constexpr uint32_t kColorFooterBg = 0xFFFBFBFC;
constexpr uint32_t kColorSeparator = 0xFFEDEEF1;
constexpr uint32_t kColorPageLabel = 0xFFAAB0B8;

}  // namespace

bool CandidateWindow::EnsureCreated() {
  if (hwnd() != nullptr) return true;
  CreateParams params;
  params.title = L"PaizaCandidates";
  if (!Create(params)) return false;

  auto list = std::make_unique<CandidateListView>();
  list->SetOnChoose([this](size_t index) {
    if (on_chosen_) on_chosen_(index);
  });
  list_ = AddWidget(std::move(list));

  auto page_label = std::make_unique<Label>();
  page_label->style().font_family = L"Segoe UI";
  page_label->style().font_size = 10.5f;
  page_label->style().color = kColorPageLabel;
  page_label->style().center_horizontal = true;
  page_label->style().center_vertical = true;
  page_label_ = AddWidget(std::move(page_label));

  auto prev = std::make_unique<Button>(L"‹");
  prev->style().font_size = 12.0f;
  prev->SetOnClick([this] {
    if (list_->PrevPage()) Relayout();
  });
  prev_button_ = AddWidget(std::move(prev));

  auto next = std::make_unique<Button>(L"›");
  next->style().font_size = 12.0f;
  next->SetOnClick([this] {
    if (list_->NextPage()) Relayout();
  });
  next_button_ = AddWidget(std::move(next));
  return true;
}

void CandidateWindow::SetCandidates(std::vector<Candidate> candidates) {
  if (!EnsureCreated()) return;
  list_->SetCandidates(std::move(candidates));
  if (list_->empty()) {
    Hide();
    return;
  }
  Relayout();
  Show();
}

bool CandidateWindow::HasCandidates() const {
  return list_ != nullptr && !list_->empty();
}

bool CandidateWindow::SelectByDigit(int digit, size_t* chosen_index) {
  if (list_ == nullptr) return false;
  size_t index = 0;
  if (!list_->DigitToIndex(digit, &index)) return false;
  *chosen_index = index;
  return true;
}

void CandidateWindow::MoveSelection(int delta) {
  if (list_ == nullptr) return;
  const size_t page_before = list_->current_page();
  if (delta >= 0) {
    list_->SelectNext();
  } else {
    list_->SelectPrev();
  }
  // Crossing a page boundary changes the page's measured size.
  if (list_->current_page() != page_before) {
    Relayout();
  } else {
    UpdatePageLabel();
  }
}

void CandidateWindow::PageForward() {
  if (list_ != nullptr && list_->NextPage()) Relayout();
}

void CandidateWindow::PageBackward() {
  if (list_ != nullptr && list_->PrevPage()) Relayout();
}

size_t CandidateWindow::selected_index() const {
  return list_ != nullptr ? list_->selected_index() : 0;
}

const Candidate* CandidateWindow::selected_candidate() const {
  if (list_ == nullptr || list_->empty()) return nullptr;
  return &list_->candidates()[list_->selected_index()];
}

void CandidateWindow::SetFontFamily(const std::wstring& font_family) {
  if (!EnsureCreated()) return;
  if (font_family.empty() || list_->font_family() == font_family) return;
  list_->set_font_family(font_family);
  if (!list_->empty()) Relayout();
}

void CandidateWindow::OnDpiChanged() {
  if (list_ != nullptr && !list_->empty()) Relayout();
}

void CandidateWindow::Relayout() {
  // The window is sized from the measured content of the *current page*
  // (fix: a fixed-size window used to clip long vertical words).
  // Layout math is logical; the window itself is sized in physical pixels.
  const Size content = list_->MeasureContent();
  // The paging footer only appears when there is more than one page, so a
  // single page of candidates stays as tight as possible.
  has_footer_ = list_->page_count() > 1;
  const int footer_min_width = kPageLabelWidth + 2 * kPageButtonWidth + 12 +
                               2 * kPadding;
  const int content_width = content.width + 2 * kPadding;
  const int width = has_footer_
                        ? std::max(content_width, footer_min_width)
                        : content_width;
  const int height = kPadding + content.height + kPadding +
                     (has_footer_ ? kFooterHeight : 0);

  // Centre the candidate strip when the footer forces extra width.
  const int list_x = (width - 2 * kPadding - content.width) / 2 + kPadding;
  list_->set_bounds({list_x, kPadding, content.width, content.height});

  if (has_footer_) {
    // Footer, bottom-right: [1/3] ‹ ›
    const int footer_y = height - kFooterHeight;
    const int button_y = footer_y + (kFooterHeight - kPageButtonHeight) / 2;
    next_button_->set_bounds({width - kPadding - kPageButtonWidth, button_y,
                              kPageButtonWidth, kPageButtonHeight});
    prev_button_->set_bounds(
        {width - kPadding - 2 * kPageButtonWidth - 4, button_y,
         kPageButtonWidth, kPageButtonHeight});
    page_label_->set_bounds(
        {width - kPadding - 2 * kPageButtonWidth - 8 - kPageLabelWidth,
         button_y, kPageLabelWidth, kPageButtonHeight});
  }
  UpdatePageLabel();

  const Rect current = GetBounds();
  SetBounds({current.x, current.y, ToPhysical(width), ToPhysical(height)});
  Invalidate();
}

void CandidateWindow::UpdatePageLabel() {
  if (page_label_ == nullptr || list_ == nullptr) return;
  const size_t pages = std::max<size_t>(list_->page_count(), 1);
  page_label_->SetText(std::to_wstring(list_->current_page() + 1) + L"/" +
                       std::to_wstring(pages));
  const bool multi_page = pages > 1;
  page_label_->set_visible(multi_page);
  prev_button_->set_visible(multi_page);
  next_button_->set_visible(multi_page);
  prev_button_->SetEnabled(multi_page && list_->current_page() > 0);
  next_button_->SetEnabled(multi_page &&
                           list_->current_page() + 1 < pages);
  Invalidate();
}

void CandidateWindow::MoveNearCaret(const RECT& caret_screen_rect) {
  if (hwnd() == nullptr) return;
  // Physical-pixel space for the monitor queries and positioning, even if
  // the host thread is DPI-virtualized.
  ScopedPerMonitorDpi dpi_scope;
  const Rect bounds = GetBounds();

  int x = caret_screen_rect.right + kCaretGap;
  int y = caret_screen_rect.top;

  // Clamp to the work area of the monitor containing the caret.
  HMONITOR monitor =
      ::MonitorFromRect(&caret_screen_rect, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info = {sizeof(info)};
  if (::GetMonitorInfoW(monitor, &info)) {
    const RECT& work = info.rcWork;
    if (x + bounds.width > work.right) {
      // Flip to the left side of the caret.
      x = caret_screen_rect.left - kCaretGap - bounds.width;
      if (x < work.left) x = work.right - bounds.width;
    }
    y = std::clamp(y, static_cast<int>(work.top),
                   static_cast<int>(work.bottom) - bounds.height);
  }
  SetBounds({x, y, bounds.width, bounds.height});
}

void CandidateWindow::OnPaintBackground(RenderContext& context) {
  context.Clear(background_color());
  if (!has_footer_) return;
  const Size client = logical_client_size();
  // Light footer strip with a hairline separator above it.
  const Rect footer = {0, client.height - kFooterHeight, client.width,
                       kFooterHeight};
  context.FillRect(footer, kColorFooterBg);
  context.DrawLine({0, footer.y}, {client.width, footer.y}, kColorSeparator,
                   1.0f);
}

void CandidateWindow::OnPaintOverlay(RenderContext& context) {
  const Size client = logical_client_size();
  context.DrawBorder({0, 0, client.width, client.height}, kColorBorder);
}

}  // namespace paiza::ui
