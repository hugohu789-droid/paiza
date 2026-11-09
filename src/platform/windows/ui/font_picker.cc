// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "ui/font_picker.h"

#include <windowsx.h>

#include <algorithm>
#include <cwctype>

#include "ui/dpi.h"
#include "ui/widget.h"

namespace paiza::ui {
namespace {

constexpr int kPadding = 6;
constexpr int kTextPadding = 12;
// Wide enough to hit with a mouse. The 5px cosmetic bar this replaces was
// the real reason the list "could not be dragged": at 5 logical pixels it
// was not a control, it was a progress indicator.
constexpr int kScrollbarWidth = 12;
constexpr int kThumbWidth = 6;
constexpr int kMinThumb = 24;

constexpr uint32_t kColorBorder = 0xFFC9CCD1;
constexpr uint32_t kColorHoverBg = 0xFFF1F6FE;
constexpr uint32_t kColorChosenBg = 0xFFD7E6FD;
constexpr uint32_t kColorHighlight = 0xFF3B7DDD;
constexpr uint32_t kColorText = 0xFF202124;
constexpr uint32_t kColorDim = 0xFF8A8F98;
constexpr uint32_t kColorBadge = 0xFF5F8ED0;
constexpr uint32_t kColorScrollbar = 0xFFB4B8BE;
constexpr uint32_t kColorTrack = 0xFFF0F1F3;
constexpr uint32_t kColorFieldBg = 0xFFF7F8FA;
constexpr uint32_t kColorFieldFocus = 0xFFFFFFFF;

std::wstring Lowered(const std::wstring& s) {
  std::wstring out;
  out.reserve(s.size());
  for (wchar_t c : s) {
    out.push_back(static_cast<wchar_t>(std::towlower(c)));
  }
  return out;
}

TextStyle UiText(float size, uint32_t color) {
  TextStyle style;
  style.font_family = L"Segoe UI";
  style.font_size = size;
  style.color = color;
  style.center_vertical = true;
  return style;
}

}  // namespace

// ---------------------------------------------------------------------------
// The filter field.
// ---------------------------------------------------------------------------
class FontFilterWidget : public Widget {
 public:
  explicit FontFilterWidget(FontPickerPopup* popup) : popup_(popup) {}

  void OnPaint(RenderContext& context) override {
    const bool focused = popup_->focus_ == FontPickerPopup::Focus::kFilter;
    context.FillRoundedRect(bounds(), 4.0f,
                            focused ? kColorFieldFocus : kColorFieldBg);
    context.DrawBorder(bounds(), focused ? kColorHighlight : kColorBorder,
                       focused ? 1.5f : 1.0f);
    const Rect text_rect = {bounds().x + kTextPadding, bounds().y,
                            bounds().width - 2 * kTextPadding,
                            bounds().height};
    const TextStyle style = UiText(13.0f, kColorText);
    if (popup_->filter_.empty()) {
      context.DrawTextBlock(popup_->placeholder_, text_rect,
                            UiText(13.0f, kColorDim));
    } else {
      context.DrawTextBlock(popup_->filter_, text_rect, style);
    }
    // Caret after the text, so it is visible that this field is typed into
    // (there is no real text layout here: the field is append/backspace
    // only, which is all a filter needs).
    if (focused) {
      const Size measured = RenderContext::MeasureText(
          popup_->filter_, style, {text_rect.width, text_rect.height});
      const int x = std::min(text_rect.x + measured.width + 1,
                             text_rect.right() - 1);
      context.FillRect({x, bounds().y + 6, 1, bounds().height - 12},
                       kColorText);
    }
  }

  void HandleEvent(Event& event) override {
    if (event.type() != EventType::kMouseDown) return;
    if (popup_->focus_ != FontPickerPopup::Focus::kFilter) {
      popup_->focus_ = FontPickerPopup::Focus::kFilter;
      Invalidate();
    }
  }

 private:
  FontPickerPopup* popup_;
};

// ---------------------------------------------------------------------------
// The list, with a scrollbar that is an actual control.
// ---------------------------------------------------------------------------
class FontListWidget : public Widget {
 public:
  explicit FontListWidget(FontPickerPopup* popup) : popup_(popup) {}

  void OnPaint(RenderContext& context) override {
    const TextStyle name_style = UiText(13.0f, kColorText);
    TextStyle badge_style = UiText(13.0f, kColorBadge);
    badge_style.font_family = L"Noto Sans Mongolian";

    const size_t rows = popup_->VisibleRows();
    const size_t first = popup_->first_visible_;
    const size_t last = std::min(first + rows, popup_->visible_.size());
    const bool overflow = popup_->visible_.size() > rows;
    const int row_width =
        bounds().width - (overflow ? kScrollbarWidth : 0);

    for (size_t slot = first; slot < last; ++slot) {
      const size_t family = popup_->visible_[slot];
      const int y = bounds().y + static_cast<int>(slot - first) *
                                     FontPickerPopup::kItemHeight;
      const Rect row = {bounds().x, y, row_width,
                        FontPickerPopup::kItemHeight};
      if (family == popup_->chosen_) {
        context.FillRect(row, kColorChosenBg);
      } else if (slot == popup_->hovered_) {
        context.FillRect(row, kColorHoverBg);
      }
      // The keyboard's position is drawn as an outline, so it reads
      // separately from "this is the font in use" (the fill above): with
      // Space meaning select *and* deselect, the two have to be
      // distinguishable or the key does something invisible.
      if (family == popup_->highlight_) {
        context.DrawBorder(row, kColorHighlight, 1.5f);
      }
      const Rect text_rect = {row.x + kTextPadding, row.y,
                              row.width - 2 * kTextPadding - 24, row.height};
      context.DrawTextBlock(popup_->families_[family].name, text_rect,
                            name_style);
      if (popup_->families_[family].supports_mongolian) {
        context.DrawTextBlock(L"\x1820", {row.right() - 26, row.y, 22,
                                          row.height},
                              badge_style);
      }
    }

    if (!overflow) return;
    const Rect track = ScrollbarTrack();
    context.FillRect(track, kColorTrack);
    const Rect thumb = ThumbRect();
    context.FillRoundedRect(thumb, static_cast<float>(kThumbWidth) / 2.0f,
                            kColorScrollbar);
  }

  void HandleEvent(Event& event) override {
    auto* mouse = dynamic_cast<MouseEvent*>(&event);
    if (mouse == nullptr) return;
    const Point p = mouse->position();
    switch (event.type()) {
      case EventType::kMouseDown:
        if (Overflowing() && ScrollbarTrack().Contains(p)) {
          const Rect thumb = ThumbRect();
          if (p.y >= thumb.y && p.y < thumb.bottom()) {
            popup_->dragging_thumb_ = true;
            popup_->drag_grab_offset_ = p.y - thumb.y;
          } else {
            // Click above or below the thumb pages towards the click, the
            // way a real scrollbar track does.
            popup_->ScrollBy(p.y < thumb.y
                                 ? -static_cast<int>(popup_->VisibleRows())
                                 : static_cast<int>(popup_->VisibleRows()));
          }
          event.set_handled();
          return;
        }
        if (popup_->focus_ != FontPickerPopup::Focus::kList) {
          popup_->focus_ = FontPickerPopup::Focus::kList;
          Invalidate();
        }
        return;
      case EventType::kMouseMove:
        if (popup_->dragging_thumb_) {
          DragTo(p.y);
          return;
        }
        if (HitSlot(p) != popup_->hovered_) {
          popup_->hovered_ = HitSlot(p);
          Invalidate();
        }
        return;
      case EventType::kMouseUp:
        popup_->drag_release_ = popup_->dragging_thumb_;
        popup_->dragging_thumb_ = false;
        return;
      case EventType::kMouseLeave:
        popup_->hovered_ = SIZE_MAX;
        Invalidate();
        return;
      case EventType::kClick: {
        if (popup_->drag_release_) {
          popup_->drag_release_ = false;
          return;
        }
        if (Overflowing() && ScrollbarTrack().Contains(p)) return;
        const size_t slot = HitSlot(p);
        if (slot == SIZE_MAX) return;
        event.set_handled();
        popup_->highlight_ = popup_->visible_[slot];
        const std::wstring name =
            popup_->families_[popup_->visible_[slot]].name;
        popup_->Hide();
        if (popup_->on_select_) popup_->on_select_(name);
        return;
      }
      default:
        return;
    }
  }

 private:
  bool Overflowing() const {
    return popup_->visible_.size() > popup_->VisibleRows();
  }

  Rect ScrollbarTrack() const {
    return {bounds().right() - kScrollbarWidth, bounds().y, kScrollbarWidth,
            bounds().height};
  }

  Rect ThumbRect() const {
    const Rect track = ScrollbarTrack();
    const size_t rows = popup_->VisibleRows();
    const size_t total = popup_->visible_.size();
    const int height = std::max(
        kMinThumb, static_cast<int>(track.height * (static_cast<float>(rows) /
                                                    total)));
    const size_t max_first = total - rows;
    const int offset = max_first == 0
                           ? 0
                           : static_cast<int>((track.height - height) *
                                              (static_cast<float>(
                                                   popup_->first_visible_) /
                                               max_first));
    return {track.x + (kScrollbarWidth - kThumbWidth) / 2, track.y + offset,
            kThumbWidth, height};
  }

  void DragTo(int y) {
    const Rect track = ScrollbarTrack();
    const size_t rows = popup_->VisibleRows();
    const size_t total = popup_->visible_.size();
    if (total <= rows) return;
    const int height = ThumbRect().height;
    const int span = track.height - height;
    if (span <= 0) return;
    const int top = std::clamp(y - popup_->drag_grab_offset_ - track.y, 0,
                               span);
    const size_t max_first = total - rows;
    const size_t first = static_cast<size_t>(
        static_cast<float>(top) / span * max_first + 0.5f);
    if (first != popup_->first_visible_) {
      popup_->first_visible_ = std::min(first, max_first);
      Invalidate();
    }
  }

  // Index into popup_->visible_, or SIZE_MAX.
  size_t HitSlot(Point p) const {
    if (!bounds().Contains(p)) return SIZE_MAX;
    const size_t row = static_cast<size_t>((p.y - bounds().y) /
                                           FontPickerPopup::kItemHeight);
    const size_t slot = popup_->first_visible_ + row;
    return slot < popup_->visible_.size() ? slot : SIZE_MAX;
  }

  FontPickerPopup* popup_;
};

// ---------------------------------------------------------------------------

void FontPickerPopup::SetFamilies(std::vector<FontFamilyInfo> families,
                                  const std::wstring& chosen,
                                  const std::wstring& effective) {
  families_ = std::move(families);
  chosen_ = SIZE_MAX;
  highlight_ = SIZE_MAX;
  for (size_t i = 0; i < families_.size(); ++i) {
    if (!chosen.empty() && families_[i].name == chosen) chosen_ = i;
    if (families_[i].name == effective) highlight_ = i;
  }
  if (highlight_ == SIZE_MAX) highlight_ = chosen_;
  filter_.clear();
  focus_ = Focus::kFilter;
  hovered_ = SIZE_MAX;
  dragging_thumb_ = false;
  ApplyFilter();
}

void FontPickerPopup::ApplyFilter() {
  const std::wstring needle = Lowered(filter_);
  visible_.clear();
  visible_.reserve(families_.size());
  for (size_t i = 0; i < families_.size(); ++i) {
    if (needle.empty() ||
        Lowered(families_[i].name).find(needle) != std::wstring::npos) {
      visible_.push_back(i);
    }
  }
  // Keep the keyboard somewhere meaningful: on the previous highlight when
  // it survived the filter, otherwise on the first match.
  const bool kept =
      highlight_ != SIZE_MAX &&
      std::find(visible_.begin(), visible_.end(), highlight_) !=
          visible_.end();
  if (!kept) highlight_ = visible_.empty() ? SIZE_MAX : visible_.front();
  hovered_ = SIZE_MAX;
  // Open with the highlight a few rows down rather than pinned to the top
  // or (as ScrollHighlightIntoView alone would do) to the bottom edge.
  const auto at = std::find(visible_.begin(), visible_.end(), highlight_);
  const size_t slot = at == visible_.end()
                          ? 0
                          : static_cast<size_t>(at - visible_.begin());
  first_visible_ = slot > 5 ? slot - 5 : 0;
  ClampScroll();
  Invalidate();
}

void FontPickerPopup::SetHints(std::wstring filter_placeholder,
                               std::wstring keys_hint) {
  placeholder_ = std::move(filter_placeholder);
  keys_hint_ = std::move(keys_hint);
}

size_t FontPickerPopup::VisibleRows() const {
  return static_cast<size_t>(kVisibleItems);
}

void FontPickerPopup::ClampScroll() {
  const size_t rows = VisibleRows();
  const size_t max_first =
      visible_.size() > rows ? visible_.size() - rows : 0;
  first_visible_ = std::min(first_visible_, max_first);
}

void FontPickerPopup::ScrollBy(int rows) {
  if (rows < 0 && first_visible_ < static_cast<size_t>(-rows)) {
    first_visible_ = 0;
  } else {
    first_visible_ += rows;
  }
  ClampScroll();
  Invalidate();
}

void FontPickerPopup::MoveHighlight(int delta) {
  if (visible_.empty()) return;
  auto it = std::find(visible_.begin(), visible_.end(), highlight_);
  const int current =
      it == visible_.end() ? -1 : static_cast<int>(it - visible_.begin());
  const int last = static_cast<int>(visible_.size()) - 1;
  const int next = std::clamp(current + delta, 0, last);
  highlight_ = visible_[static_cast<size_t>(next)];
  ScrollHighlightIntoView();
  Invalidate();
}

void FontPickerPopup::ScrollHighlightIntoView() {
  auto it = std::find(visible_.begin(), visible_.end(), highlight_);
  if (it == visible_.end()) {
    ClampScroll();
    return;
  }
  const size_t slot = static_cast<size_t>(it - visible_.begin());
  const size_t rows = VisibleRows();
  if (slot < first_visible_) {
    first_visible_ = slot;
  } else if (slot >= first_visible_ + rows) {
    first_visible_ = slot - rows + 1;
  }
  ClampScroll();
}

void FontPickerPopup::ToggleHighlighted() {
  if (highlight_ == SIZE_MAX) return;
  if (highlight_ == chosen_) {
    // Deselect: the profile goes back to the font it declares. This is a
    // state the model already has (an empty per-profile value), and it is
    // the only way to undo a choice without knowing what the default was.
    chosen_ = SIZE_MAX;
    Hide();
    if (on_clear_) on_clear_();
    return;
  }
  const std::wstring name = families_[highlight_].name;
  chosen_ = highlight_;
  Hide();
  if (on_select_) on_select_(name);
}

void FontPickerPopup::Dismiss() { Hide(); }

void FontPickerPopup::ShowBelow(HWND owner, const Rect& anchor_physical) {
  if (hwnd() == nullptr) {
    CreateParams params;
    params.title = L"PaizaFontPicker";
    params.parent = owner;
    // No WS_EX_NOACTIVATE, unlike every other popup here: see the header.
    params.ex_style = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    if (!Create(params)) return;
  }
  if (!widgets_built_) {
    widgets_built_ = true;
    AddWidget(std::make_unique<FontFilterWidget>(this));
    AddWidget(std::make_unique<FontListWidget>(this));
  }
  const int list_y = kPadding + kFilterHeight + kPadding;
  const int list_height = static_cast<int>(VisibleRows()) * kItemHeight;
  widgets()[0]->set_bounds(
      {kPadding, kPadding, kWidth - 2 * kPadding, kFilterHeight});
  widgets()[1]->set_bounds({0, list_y, kWidth, list_height});
  const int height_logical = list_y + list_height + kHintHeight;

  ScopedPerMonitorDpi dpi_scope;
  int x = anchor_physical.x;
  int y = anchor_physical.bottom() + 2;
  const int width = ToPhysical(kWidth);
  const int height = ToPhysical(height_logical);

  RECT anchor_rect = {anchor_physical.x, anchor_physical.y,
                      anchor_physical.right(), anchor_physical.bottom()};
  HMONITOR monitor =
      ::MonitorFromRect(&anchor_rect, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info = {sizeof(info)};
  if (::GetMonitorInfoW(monitor, &info)) {
    const RECT& work = info.rcWork;
    x = std::clamp(x, static_cast<int>(work.left),
                   static_cast<int>(work.right) - width);
    if (y + height > work.bottom) {
      y = anchor_physical.y - 2 - height;  // Open upwards.
    }
  }
  SetBounds({x, y, width, height});
  Show();
  // Taking focus is the whole point (see the header): the wheel and every
  // key below go to the focused window.
  ::SetFocus(hwnd());
  Invalidate();
}

bool FontPickerPopup::OnMessage(UINT message, WPARAM wparam, LPARAM lparam,
                                LRESULT* result) {
  switch (message) {
    case WM_MOUSEWHEEL: {
      // Three rows per notch, and every notch the message carries: a
      // high-resolution wheel sends several at once.
      const int notches = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
      if (notches != 0) ScrollBy(-3 * notches);
      *result = 0;
      return true;
    }
    case WM_ACTIVATE:
      // Clicking anywhere outside closes the picker, which is what the
      // settings dialog used to do by hand for the non-focusable popups.
      if (LOWORD(wparam) == WA_INACTIVE) Hide();
      break;
    case WM_KEYDOWN:
      switch (wparam) {
        case VK_TAB:
          focus_ = focus_ == Focus::kFilter ? Focus::kList : Focus::kFilter;
          Invalidate();
          *result = 0;
          return true;
        case VK_DOWN:
        case VK_UP:
          // An arrow key is also how the keyboard leaves the filter field:
          // the alternative is a Tab that the user has to know about.
          focus_ = Focus::kList;
          MoveHighlight(wparam == VK_DOWN ? 1 : -1);
          *result = 0;
          return true;
        case VK_NEXT:
        case VK_PRIOR:
          focus_ = Focus::kList;
          MoveHighlight(wparam == VK_NEXT
                            ? static_cast<int>(VisibleRows())
                            : -static_cast<int>(VisibleRows()));
          *result = 0;
          return true;
        case VK_HOME:
          if (focus_ == Focus::kList) {
            MoveHighlight(-static_cast<int>(visible_.size()));
            *result = 0;
            return true;
          }
          break;
        case VK_END:
          if (focus_ == Focus::kList) {
            MoveHighlight(static_cast<int>(visible_.size()));
            *result = 0;
            return true;
          }
          break;
        case VK_SPACE:
          // Only in the list: a space is a character in most font names
          // ("Noto Sans Mongolian"), so in the filter field it has to type.
          if (focus_ == Focus::kList) {
            ToggleHighlighted();
            *result = 0;
            return true;
          }
          break;
        case VK_RETURN:
          if (highlight_ != SIZE_MAX) {
            const std::wstring name = families_[highlight_].name;
            chosen_ = highlight_;
            Hide();
            if (on_select_) on_select_(name);
          } else {
            Dismiss();
          }
          *result = 0;
          return true;
        case VK_ESCAPE:
          Dismiss();
          *result = 0;
          return true;
        case VK_BACK:
          if (!filter_.empty()) {
            filter_.pop_back();
            ApplyFilter();
          }
          focus_ = Focus::kFilter;
          *result = 0;
          return true;
        default:
          break;
      }
      break;
    case WM_CHAR: {
      const wchar_t c = static_cast<wchar_t>(wparam);
      if (c < 0x20) break;  // Control characters: handled in WM_KEYDOWN.
      // A space reaching here while the list has focus is the toggle key's
      // WM_CHAR (TranslateMessage synthesizes it regardless of what
      // WM_KEYDOWN did with it), so it must not also be typed.
      if (c == L' ' && focus_ == Focus::kList) {
        *result = 0;
        return true;
      }
      // Typing with the list focused goes back to the filter: that is what
      // the user means, and it is how every other filtered list behaves.
      focus_ = Focus::kFilter;
      filter_.push_back(c);
      ApplyFilter();
      *result = 0;
      return true;
    }
    default:
      break;
  }
  return Window::OnMessage(message, wparam, lparam, result);
}

void FontPickerPopup::OnPaintBackground(RenderContext& context) {
  context.Clear(0xFFFFFFFF);
}

void FontPickerPopup::OnPaintOverlay(RenderContext& context) {
  const Size client = logical_client_size();
  const Rect hint = {kTextPadding, client.height - kHintHeight,
                     client.width - 2 * kTextPadding, kHintHeight};
  context.FillRect({0, hint.y, client.width, kHintHeight}, kColorFieldBg);
  context.DrawLine({0, hint.y}, {client.width, hint.y}, kColorBorder, 1.0f);
  if (!keys_hint_.empty()) {
    context.DrawTextBlock(keys_hint_, hint, UiText(11.0f, kColorDim));
  }
  context.DrawBorder({0, 0, client.width, client.height}, kColorBorder);
}

}  // namespace paiza::ui
