// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// DropdownPopup: a scrollable single-select list popup, used by the
// settings dialog's font picker. Anchored below a trigger button, closed
// by selecting an item or by the owner (e.g. on an outside click).

#ifndef PAIZA_UI_DROPDOWN_H_
#define PAIZA_UI_DROPDOWN_H_

#include <functional>
#include <string>
#include <vector>

#include "ui/window.h"

namespace paiza::ui {

struct DropdownItem {
  std::wstring text;
  // Optional marker drawn right-aligned (e.g. "ᠠ" for Mongolian-capable
  // fonts in the font picker).
  std::wstring badge;
};

class DropdownPopup : public Window {
 public:
  DropdownPopup() = default;

  void SetItems(std::vector<DropdownItem> items, size_t selected_index);
  void SetOnSelect(std::function<void(size_t)> on_select) {
    on_select_ = std::move(on_select);
  }

  // Shows the popup below `anchor` (physical screen rect), clamped to the
  // work area. `owner` keeps the popup above the settings dialog.
  void ShowBelow(HWND owner, const Rect& anchor_physical);

  static constexpr int kItemHeight = 26;    // Logical units.
  static constexpr int kVisibleItems = 12;
  static constexpr int kWidth = 300;

 protected:
  bool OnMessage(UINT message, WPARAM wparam, LPARAM lparam,
                 LRESULT* result) override;
  void OnPaintBackground(RenderContext& context) override;
  void OnPaintOverlay(RenderContext& context) override;

 private:
  friend class DropdownListWidget;

  size_t VisibleCount() const;
  void ScrollBy(int items);
  void ClampScroll();

  std::vector<DropdownItem> items_;
  size_t selected_index_ = 0;
  size_t hovered_index_ = SIZE_MAX;
  size_t first_visible_ = 0;
  std::function<void(size_t)> on_select_;
  bool widgets_built_ = false;
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_DROPDOWN_H_
