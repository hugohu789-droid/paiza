// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Static text control supporting horizontal and vertical orientation.

#ifndef PAIZA_UI_LABEL_H_
#define PAIZA_UI_LABEL_H_

#include <string>

#include "ui/widget.h"

namespace paiza::ui {

class Label : public Widget {
 public:
  Label() = default;
  explicit Label(std::wstring text) : text_(std::move(text)) {}

  void SetText(std::wstring text);
  const std::wstring& text() const { return text_; }

  TextStyle& style() { return style_; }
  const TextStyle& style() const { return style_; }

  // Preferred size of the current text within `max` bounds.
  Size Measure(Size max) const;

  // Widget:
  void OnPaint(RenderContext& context) override;

 private:
  std::wstring text_;
  TextStyle style_;
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_LABEL_H_
