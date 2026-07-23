// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "ui/dropdown.h"

#include <windowsx.h>

#include <algorithm>

#include "ui/dpi.h"
#include "ui/widget.h"

namespace paiza::ui {
namespace {

constexpr int kPaddingY = 4;
constexpr int kTextPadding = 12;
constexpr int kScrollbarWidth = 5;

constexpr uint32_t kColorBorder = 0xFFC9CCD1;
constexpr uint32_t kColorHoverBg = 0xFFF1F6FE;
constexpr uint32_t kColorSelectedBg = 0xFFD7E6FD;
constexpr uint32_t kColorText = 0xFF202124;
constexpr uint32_t kColorBadge = 0xFF5F8ED0;
constexpr uint32_t kColorScrollbar = 0xFFD0D3D8;

}  // namespace

// Fills the popup's client area; delegates state to the owning popup.
class DropdownListWidget : public Widget {
 public:
  explicit DropdownListWidget(DropdownPopup* popup) : popup_(popup) {}

  void OnPaint(RenderContext& context) override {
    TextStyle text_style;
    text_style.font_family = L"Segoe UI";
    text_style.font_size = 13.0f;
    text_style.color = kColorText;
    text_style.center_vertical = true;
    TextStyle badge_style = text_style;
    badge_style.font_family = L"Noto Sans Mongolian";
    badge_style.color = kColorBadge;

    const auto& items = popup_->items_;
    const size_t first = popup_->first_visible_;
    const size_t last =
        std::min(first + popup_->VisibleCount(), items.size());
    for (size_t i = first; i < last; ++i) {
      const int y = bounds().y +
                    static_cast<int>(i - first) * DropdownPopup::kItemHeight;
      const Rect row = {bounds().x, y, bounds().width,
                        DropdownPopup::kItemHeight};
      if (i == popup_->selected_index_) {
        context.FillRect(row, kColorSelectedBg);
      } else if (i == popup_->hovered_index_) {
        context.FillRect(row, kColorHoverBg);
      }
      const Rect text_rect = {row.x + kTextPadding, row.y,
                              row.width - 2 * kTextPadding -
                                  kScrollbarWidth - 24,
                              row.height};
      context.DrawTextBlock(items[i].text, text_rect, text_style);
      if (!items[i].badge.empty()) {
        const Rect badge_rect = {row.right() - kScrollbarWidth - 28, row.y,
                                 24, row.height};
        context.DrawTextBlock(items[i].badge, badge_rect, badge_style);
      }
    }

    // Proportional scrollbar when the list overflows.
    if (items.size() > popup_->VisibleCount()) {
      const float ratio =
          static_cast<float>(popup_->VisibleCount()) / items.size();
      const int track = bounds().height;
      const int thumb = std::max(20, static_cast<int>(track * ratio));
      const int offset = static_cast<int>(
          (track - thumb) * static_cast<float>(popup_->first_visible_) /
          (items.size() - popup_->VisibleCount()));
      context.FillRoundedRect({bounds().right() - kScrollbarWidth - 2,
                               bounds().y + offset, kScrollbarWidth, thumb},
                              2.0f, kColorScrollbar);
    }
  }

  void HandleEvent(Event& event) override {
    auto* mouse = dynamic_cast<MouseEvent*>(&event);
    if (mouse == nullptr) return;
    const size_t index = HitIndex(mouse->position());
    switch (event.type()) {
      case EventType::kMouseMove:
        if (index != popup_->hovered_index_) {
          popup_->hovered_index_ = index;
          Invalidate();
        }
        break;
      case EventType::kMouseLeave:
        popup_->hovered_index_ = SIZE_MAX;
        Invalidate();
        break;
      case EventType::kClick:
        if (index != SIZE_MAX) {
          event.set_handled();
          popup_->Hide();
          if (popup_->on_select_) popup_->on_select_(index);
        }
        break;
      default:
        break;
    }
  }

 private:
  size_t HitIndex(Point p) const {
    if (!bounds().Contains(p)) return SIZE_MAX;
    const size_t row = static_cast<size_t>(
        (p.y - bounds().y) / DropdownPopup::kItemHeight);
    const size_t index = popup_->first_visible_ + row;
    return index < popup_->items_.size() ? index : SIZE_MAX;
  }

  DropdownPopup* popup_;
};

void DropdownPopup::SetItems(std::vector<DropdownItem> items,
                             size_t selected_index) {
  items_ = std::move(items);
  selected_index_ = selected_index < items_.size() ? selected_index : 0;
  hovered_index_ = SIZE_MAX;
  // Scroll the selection roughly into the middle.
  first_visible_ =
      selected_index_ > 5 ? selected_index_ - 5 : 0;
  ClampScroll();
}

size_t DropdownPopup::VisibleCount() const {
  return std::min<size_t>(kVisibleItems, std::max<size_t>(items_.size(), 1));
}

void DropdownPopup::ClampScroll() {
  const size_t max_first =
      items_.size() > VisibleCount() ? items_.size() - VisibleCount() : 0;
  first_visible_ = std::min(first_visible_, max_first);
}

void DropdownPopup::ScrollBy(int items) {
  if (items < 0 && first_visible_ < static_cast<size_t>(-items)) {
    first_visible_ = 0;
  } else {
    first_visible_ += items;
  }
  ClampScroll();
  Invalidate();
}

void DropdownPopup::ShowBelow(HWND owner, const Rect& anchor_physical) {
  if (hwnd() == nullptr) {
    CreateParams params;
    params.title = L"PaizaDropdown";
    params.parent = owner;
    if (!Create(params)) return;
  }
  if (!widgets_built_) {
    widgets_built_ = true;
    AddWidget(std::make_unique<DropdownListWidget>(this));
  }
  const int height_logical =
      static_cast<int>(VisibleCount()) * kItemHeight + 2 * kPaddingY;
  widgets()[0]->set_bounds(
      {0, kPaddingY, kWidth, height_logical - 2 * kPaddingY});

  ScopedPerMonitorDpi dpi_scope;
  int x = anchor_physical.x;
  int y = anchor_physical.bottom() + 2;
  const int width = ToPhysical(kWidth);
  const int height = ToPhysical(height_logical);

  RECT anchor_rect = {anchor_physical.x, anchor_physical.y,
                      anchor_physical.right(), anchor_physical.bottom()};
  HMONITOR monitor = ::MonitorFromRect(&anchor_rect,
                                       MONITOR_DEFAULTTONEAREST);
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
  Invalidate();
}

bool DropdownPopup::OnMessage(UINT message, WPARAM wparam, LPARAM lparam,
                              LRESULT* result) {
  if (message == WM_MOUSEWHEEL) {
    const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
    ScrollBy(delta > 0 ? -3 : 3);
    *result = 0;
    return true;
  }
  return Window::OnMessage(message, wparam, lparam, result);
}

void DropdownPopup::OnPaintBackground(RenderContext& context) {
  context.Clear(0xFFFFFFFF);
}

void DropdownPopup::OnPaintOverlay(RenderContext& context) {
  const Size client = logical_client_size();
  context.DrawBorder({0, 0, client.width, client.height}, kColorBorder);
}

}  // namespace paiza::ui
