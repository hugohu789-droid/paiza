// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// FontPickerPopup: the candidate-font picker of the settings dialog.
//
// A DropdownPopup with a filter field would not do, for a reason that is
// specific to this one list: a machine has a few hundred font families, so
// the list is long enough that reaching a name by scrolling is the slow
// path and typing it is the fast one. It also has to be reachable without
// a mouse at all.
//
// Unlike every other popup in this process it therefore TAKES FOCUS (no
// WS_EX_NOACTIVATE). That is what makes the keyboard and the mouse wheel
// arrive here at all: WM_KEYDOWN, WM_CHAR and WM_MOUSEWHEEL all go to the
// focused window, so a popup that refuses focus can only ever be clicked.
// Losing activation closes it, which doubles as dismiss-on-outside-click.

#ifndef PAIZA_UI_FONT_PICKER_H_
#define PAIZA_UI_FONT_PICKER_H_

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "ui/render_context.h"
#include "ui/window.h"

namespace paiza::ui {

class FontPickerPopup : public Window {
 public:
  FontPickerPopup() = default;

  // `families` is the full list; `chosen` is the family the profile on
  // display has explicitly been given, empty when it has none (then the
  // list opens on `effective` but with nothing selected, so Space can only
  // add a choice and not take one away).
  void SetFamilies(std::vector<FontFamilyInfo> families,
                   const std::wstring& chosen, const std::wstring& effective);

  // Localized: the filter field's placeholder and the one-line reminder of
  // the keys, drawn along the bottom edge. Keyboard control nobody can see
  // is keyboard control nobody uses.
  void SetHints(std::wstring filter_placeholder, std::wstring keys_hint);

  // Called with the family the user picked.
  void SetOnSelect(std::function<void(const std::wstring&)> on_select) {
    on_select_ = std::move(on_select);
  }
  // Called when the user removes their choice (Space on the selected row),
  // which puts the profile back on the font it declares.
  void SetOnClear(std::function<void()> on_clear) {
    on_clear_ = std::move(on_clear);
  }

  // Shows the popup below `anchor` (physical screen pixels), clamped to the
  // work area, and takes focus.
  void ShowBelow(HWND owner, const Rect& anchor_physical);

  static constexpr int kItemHeight = 26;    // Logical units.
  static constexpr int kVisibleItems = 12;
  static constexpr int kWidth = 340;
  static constexpr int kFilterHeight = 30;
  // Two lines: the key list does not fit on one at this
  // width, and abbreviating it away defeats the point.
  static constexpr int kHintHeight = 38;

 protected:
  bool OnMessage(UINT message, WPARAM wparam, LPARAM lparam,
                 LRESULT* result) override;
  void OnPaintBackground(RenderContext& context) override;
  void OnPaintOverlay(RenderContext& context) override;

 private:
  friend class FontFilterWidget;
  friend class FontListWidget;

  enum class Focus { kFilter, kList };

  // Rebuilds `visible_` from `filter_`, keeping the selected family
  // selected when it survives the filter.
  void ApplyFilter();
  size_t VisibleRows() const;
  void ScrollBy(int rows);
  void ClampScroll();
  // Moves the highlight by `delta` rows and scrolls it into view.
  void MoveHighlight(int delta);
  void ScrollHighlightIntoView();
  // Commits the highlighted row: chooses it, or removes the choice when it
  // is already the chosen one (the "deselect" half of Space).
  void ToggleHighlighted();
  void Dismiss();

  std::vector<FontFamilyInfo> families_;
  std::wstring placeholder_;
  std::wstring keys_hint_;
  // Indices into families_, in display order, after the filter.
  std::vector<size_t> visible_;
  std::wstring filter_;
  // Index into families_, or SIZE_MAX. `chosen_` is what the profile has
  // been given; `highlight_` is where the keyboard is.
  size_t chosen_ = SIZE_MAX;
  size_t highlight_ = SIZE_MAX;
  size_t hovered_ = SIZE_MAX;  // Index into visible_.
  size_t first_visible_ = 0;
  Focus focus_ = Focus::kFilter;
  bool dragging_thumb_ = false;
  // Set when a thumb drag ends, so the click synthesized by that same
  // mouse-up is not also read as a click on the row underneath.
  bool drag_release_ = false;
  int drag_grab_offset_ = 0;  // Logical px from the thumb's top.
  std::function<void(const std::wstring&)> on_select_;
  std::function<void()> on_clear_;
  bool widgets_built_ = false;
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_FONT_PICKER_H_
