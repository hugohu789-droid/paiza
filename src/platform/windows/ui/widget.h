// Copyright 2026 Paiza. All rights reserved.
//
// Widget: base class for all controls. Widgets are lightweight (no HWND of
// their own); the owning Window paints them into its Direct2D target and
// the EventDispatcher routes mouse events by hit-testing widget bounds.
// This is the usual design for IME popup UI, which must never steal focus.

#ifndef PAIZA_UI_WIDGET_H_
#define PAIZA_UI_WIDGET_H_

#include "ui/event.h"
#include "ui/geometry.h"
#include "ui/render_context.h"

namespace paiza::ui {

class Window;

class Widget {
 public:
  Widget() = default;
  virtual ~Widget() = default;
  Widget(const Widget&) = delete;
  Widget& operator=(const Widget&) = delete;

  // Paints the widget. Coordinates in `bounds()` are window-client space.
  virtual void OnPaint(RenderContext& context) = 0;

  // Handles a routed event. Default implementation does nothing.
  virtual void HandleEvent(Event& event) { (void)event; }

  // Hit test; default is a rectangle test against bounds.
  virtual bool HitTest(Point p) const { return visible_ && bounds_.Contains(p); }

  const Rect& bounds() const { return bounds_; }
  void set_bounds(const Rect& bounds) { bounds_ = bounds; }

  bool visible() const { return visible_; }
  void set_visible(bool visible) { visible_ = visible; }

  Window* window() const { return window_; }
  void set_window(Window* window) { window_ = window; }

  // Requests a repaint of the owning window.
  void Invalidate();

 private:
  Rect bounds_;
  bool visible_ = true;
  Window* window_ = nullptr;
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_WIDGET_H_
