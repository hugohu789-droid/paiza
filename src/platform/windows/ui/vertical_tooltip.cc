// Copyright 2026 Paiza. All rights reserved.

#include "ui/vertical_tooltip.h"

#include <algorithm>

namespace paiza::ui {
namespace {

constexpr int kPadding = 6;
constexpr int kMaxHeight = 400;

}  // namespace

void VerticalTooltip::ShowText(const std::wstring& text, Point anchor,
                               unsigned auto_hide_ms) {
  if (hwnd() == nullptr) {
    CreateParams params;
    params.title = L"PaizaTooltip";
    if (!Create(params)) return;
    auto label = std::make_unique<Label>();
    label->style().orientation = TextOrientation::kVertical;
    label->style().font_size = 16.0f;
    label_ = AddWidget(std::move(label));
  }
  label_->SetText(text);

  const Size text_size =
      label_->Measure({kMaxHeight, kMaxHeight});  // Generous bounds.
  const Rect widget_rect = {kPadding, kPadding, text_size.width,
                            std::min(text_size.height, kMaxHeight)};
  label_->set_bounds(widget_rect);
  SetBounds({anchor.x, anchor.y,
             ToPhysical(widget_rect.width + 2 * kPadding),
             ToPhysical(widget_rect.height + 2 * kPadding)});
  Show();
  Invalidate();
  if (auto_hide_ms > 0) {
    ::SetTimer(hwnd(), kHideTimerId, auto_hide_ms, nullptr);
  }
}

bool VerticalTooltip::OnMessage(UINT message, WPARAM wparam, LPARAM lparam,
                                LRESULT* result) {
  if (message == WM_TIMER && wparam == kHideTimerId) {
    ::KillTimer(hwnd(), kHideTimerId);
    Hide();
    *result = 0;
    return true;
  }
  return Window::OnMessage(message, wparam, lparam, result);
}

void VerticalTooltip::OnPaintOverlay(RenderContext& context) {
  const Size client = logical_client_size();
  context.DrawBorder({0, 0, client.width, client.height}, 0xFFD0C080);
}

}  // namespace paiza::ui
