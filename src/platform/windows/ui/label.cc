// Copyright 2026 Paiza. All rights reserved.

#include "ui/label.h"

namespace paiza::ui {

void Label::SetText(std::wstring text) {
  if (text_ == text) return;
  text_ = std::move(text);
  Invalidate();
}

Size Label::Measure(Size max) const {
  return RenderContext::MeasureText(text_, style_, max);
}

void Label::OnPaint(RenderContext& context) {
  context.DrawTextBlock(text_, bounds(), style_);
}

}  // namespace paiza::ui
