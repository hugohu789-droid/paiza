// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "ui/checkbox.h"

namespace paiza::ui {
namespace {

constexpr int kBox = 16;             // Box side, pixels.
constexpr int kGap = 8;              // Box-to-caption gap.
constexpr uint32_t kAccent = 0xFF1A73E8;
constexpr uint32_t kAccentHover = 0xFF1765CC;
constexpr uint32_t kBoxBg = 0xFFFFFFFF;
constexpr uint32_t kBorder = 0xFFC9CCD1;
constexpr uint32_t kBorderHover = 0xFF9FB6D9;
constexpr uint32_t kCheck = 0xFFFFFFFF;

}  // namespace

void Checkbox::SetCaption(std::wstring caption) {
  caption_ = std::move(caption);
  Invalidate();
}

void Checkbox::SetChecked(bool checked) {
  if (checked_ == checked) return;
  checked_ = checked;
  Invalidate();
}

void Checkbox::OnPaint(RenderContext& context) {
  const Rect b = bounds();
  const Rect box = {b.x, b.y + (b.height - kBox) / 2, kBox, kBox};

  if (checked_) {
    context.FillRoundedRect(box, 3.0f, hovered_ ? kAccentHover : kAccent);
    // A two-segment tick.
    context.DrawLine({box.x + 3, box.y + 8}, {box.x + 6, box.y + 11}, kCheck,
                     2.0f);
    context.DrawLine({box.x + 6, box.y + 11}, {box.x + 13, box.y + 4}, kCheck,
                     2.0f);
  } else {
    context.FillRoundedRect(box, 3.0f, kBoxBg);
    context.DrawBorder(box, hovered_ ? kBorderHover : kBorder);
  }

  const Rect caption_rect = {box.right() + kGap, b.y,
                             b.width - kBox - kGap, b.height};
  context.DrawTextBlock(caption_, caption_rect, style_);
}

void Checkbox::HandleEvent(Event& event) {
  switch (event.type()) {
    case EventType::kMouseEnter:
      hovered_ = true;
      Invalidate();
      break;
    case EventType::kMouseLeave:
      hovered_ = false;
      Invalidate();
      break;
    case EventType::kClick:
      event.set_handled();
      checked_ = !checked_;
      Invalidate();
      if (on_toggle_) on_toggle_(checked_);
      break;
    default:
      break;
  }
}

}  // namespace paiza::ui
