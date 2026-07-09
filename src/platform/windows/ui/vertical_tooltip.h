// Copyright 2026 Paiza. All rights reserved.
//
// A small popup window showing vertically laid-out Mongolian text, e.g. for
// annotating the composition or a hovered candidate. Auto-hides after a
// configurable delay.

#ifndef PAIZA_UI_VERTICAL_TOOLTIP_H_
#define PAIZA_UI_VERTICAL_TOOLTIP_H_

#include <string>

#include "ui/label.h"
#include "ui/window.h"

namespace paiza::ui {

class VerticalTooltip : public Window {
 public:
  VerticalTooltip() = default;

  // Shows `text` with its top-left near `anchor` (screen coordinates).
  // Creates the window on first use.
  void ShowText(const std::wstring& text, Point anchor,
                unsigned auto_hide_ms = 3000);

 protected:
  // Window:
  bool OnMessage(UINT message, WPARAM wparam, LPARAM lparam,
                 LRESULT* result) override;
  uint32_t background_color() const override { return 0xFFFFF7E0; }
  void OnPaintOverlay(RenderContext& context) override;

 private:
  static constexpr UINT_PTR kHideTimerId = 1;
  Label* label_ = nullptr;  // Owned by the window's widget list.
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_VERTICAL_TOOLTIP_H_
