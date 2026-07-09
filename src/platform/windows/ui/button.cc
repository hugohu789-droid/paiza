// Copyright 2026 Paiza. All rights reserved.

#include "ui/button.h"

namespace paiza::ui {
namespace {

// Default (bordered) palette.
constexpr uint32_t kBgNormal = 0xFFFFFFFF;
constexpr uint32_t kBgHover = 0xFFF1F6FE;
constexpr uint32_t kBgPressed = 0xFFD2E3FC;
constexpr uint32_t kBgDisabled = 0xFFF5F5F5;
constexpr uint32_t kBorder = 0xFFC9CCD1;
constexpr uint32_t kBorderHover = 0xFF9FB6D9;
constexpr uint32_t kTextDisabled = 0xFFAAAAAA;

// Primary (filled accent) palette.
constexpr uint32_t kPrimaryNormal = 0xFF1A73E8;
constexpr uint32_t kPrimaryHover = 0xFF1765CC;
constexpr uint32_t kPrimaryPressed = 0xFF14519E;
constexpr uint32_t kPrimaryDisabled = 0xFFA8C7FA;
constexpr uint32_t kPrimaryText = 0xFFFFFFFF;

constexpr float kCornerRadius = 4.0f;

}  // namespace

void Button::SetCaption(std::wstring caption) {
  caption_ = std::move(caption);
  Invalidate();
}

void Button::SetEnabled(bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  Invalidate();
}

Button::VisualState Button::visual_state() const {
  if (!enabled_) return VisualState::kDisabled;
  if (pressed_) return VisualState::kPressed;
  if (hovered_) return VisualState::kHover;
  return VisualState::kNormal;
}

void Button::OnPaint(RenderContext& context) {
  const bool primary = kind_ == Kind::kPrimary;
  uint32_t bg = primary ? kPrimaryNormal : kBgNormal;
  switch (visual_state()) {
    case VisualState::kHover:
      bg = primary ? kPrimaryHover : kBgHover;
      break;
    case VisualState::kPressed:
      bg = primary ? kPrimaryPressed : kBgPressed;
      break;
    case VisualState::kDisabled:
      bg = primary ? kPrimaryDisabled : kBgDisabled;
      break;
    case VisualState::kNormal:
      break;
  }
  context.FillRoundedRect(bounds(), kCornerRadius, bg);
  if (!primary) {
    context.DrawBorder(bounds(), hovered_ && enabled_ ? kBorderHover
                                                      : kBorder);
  }

  TextStyle text_style = style_;
  if (primary) {
    text_style.color = kPrimaryText;
  } else if (!enabled_) {
    text_style.color = kTextDisabled;
  }
  context.DrawTextBlock(caption_, bounds(), text_style);
}

void Button::HandleEvent(Event& event) {
  if (!enabled_) return;
  switch (event.type()) {
    case EventType::kMouseEnter:
      hovered_ = true;
      Invalidate();
      break;
    case EventType::kMouseLeave:
      hovered_ = false;
      pressed_ = false;
      Invalidate();
      break;
    case EventType::kMouseDown:
      pressed_ = true;
      Invalidate();
      event.set_handled();
      break;
    case EventType::kMouseUp:
      pressed_ = false;
      Invalidate();
      break;
    case EventType::kClick:
      event.set_handled();
      if (on_click_) on_click_();
      break;
    default:
      break;
  }
}

}  // namespace paiza::ui
