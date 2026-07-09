// Copyright 2026 Paiza. All rights reserved.
//
// Push button with hover/pressed visuals, horizontal or vertical caption.

#ifndef PAIZA_UI_BUTTON_H_
#define PAIZA_UI_BUTTON_H_

#include <functional>
#include <string>

#include "ui/widget.h"

namespace paiza::ui {

class Button : public Widget {
 public:
  // Visual kind: kDefault is a light button with a border, kPrimary is a
  // filled accent-coloured button (one per dialog, e.g. "Save").
  enum class Kind { kDefault, kPrimary };

  Button() = default;
  explicit Button(std::wstring caption) : caption_(std::move(caption)) {}

  void SetCaption(std::wstring caption);
  void SetOnClick(std::function<void()> on_click) {
    on_click_ = std::move(on_click);
  }
  void SetEnabled(bool enabled);
  void set_kind(Kind kind) { kind_ = kind; }

  TextStyle& style() { return style_; }

  // Widget:
  void OnPaint(RenderContext& context) override;
  void HandleEvent(Event& event) override;

 private:
  enum class VisualState { kNormal, kHover, kPressed, kDisabled };
  VisualState visual_state() const;

  std::wstring caption_;
  // Buttons show UI captions (labels, arrows), so default to a UI font
  // rather than TextStyle's Mongolian default — otherwise glyphs like the
  // ‹ › paging arrows fall back to an unrelated font and render off-centre.
  TextStyle style_ = {.font_family = L"Segoe UI",
                      .font_size = 14.0f,
                      .center_horizontal = true,
                      .center_vertical = true};
  std::function<void()> on_click_;
  Kind kind_ = Kind::kDefault;
  bool enabled_ = true;
  bool hovered_ = false;
  bool pressed_ = false;
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_BUTTON_H_
