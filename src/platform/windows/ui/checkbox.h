// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Labelled checkbox: a square box (with a check mark when on) followed by a
// caption. Toggles on click and reports the new state.

#ifndef PAIZA_UI_CHECKBOX_H_
#define PAIZA_UI_CHECKBOX_H_

#include <functional>
#include <string>

#include "ui/widget.h"

namespace paiza::ui {

class Checkbox : public Widget {
 public:
  Checkbox() = default;

  void SetCaption(std::wstring caption);
  void SetChecked(bool checked);
  bool checked() const { return checked_; }
  void SetOnToggle(std::function<void(bool)> on_toggle) {
    on_toggle_ = std::move(on_toggle);
  }

  TextStyle& style() { return style_; }

  // Widget:
  void OnPaint(RenderContext& context) override;
  void HandleEvent(Event& event) override;

 private:
  std::wstring caption_;
  bool checked_ = false;
  bool hovered_ = false;
  std::function<void(bool)> on_toggle_;
  TextStyle style_ = {.font_family = L"Segoe UI",
                      .font_size = 14.0f,
                      .center_horizontal = false,
                      .center_vertical = true};
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_CHECKBOX_H_
