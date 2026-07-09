// Copyright 2026 Paiza. All rights reserved.
//
// Window: base class wrapping an HWND. Owns a widget tree, a render
// context and an event dispatcher. Subclasses (CandidateWindow,
// VerticalTooltip, SettingsWindow) add layout and behavior.

#ifndef PAIZA_UI_WINDOW_H_
#define PAIZA_UI_WINDOW_H_

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "ui/event_dispatcher.h"
#include "ui/render_context.h"
#include "ui/widget.h"

namespace paiza::ui {

class Window {
 public:
  struct CreateParams {
    std::wstring title;
    DWORD style = WS_POPUP;
    DWORD ex_style = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    Rect bounds = {0, 0, 200, 100};
    HWND parent = nullptr;
  };

  Window();
  virtual ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  bool Create(const CreateParams& params);
  void Destroy();

  void Show();
  void Hide();
  bool IsVisible() const;
  // Outer window rectangle in PHYSICAL screen pixels.
  void SetBounds(const Rect& bounds);
  Rect GetBounds() const;
  void Invalidate();
  HWND hwnd() const { return hwnd_; }

  // DPI ------------------------------------------------------------------
  // Widget layout, painting and mouse events all use logical 96-DPI units;
  // scale() converts to the window's native pixels.
  float scale() const { return scale_; }
  int ToPhysical(int logical) const {
    return static_cast<int>(logical * scale_ + 0.5f);
  }
  // Client-area size in logical units.
  Size logical_client_size() const;

  // Adds a widget (painted in insertion order; last added is front-most for
  // hit testing). The window keeps ownership; the returned pointer stays
  // valid for the window's lifetime.
  template <typename T>
  T* AddWidget(std::unique_ptr<T> widget) {
    T* raw = widget.get();
    raw->set_window(this);
    widgets_.push_back(std::move(widget));
    dispatcher_.SetWidgets(&widgets_);
    return raw;
  }

 protected:
  // Subclass hooks -----------------------------------------------------
  virtual void OnCreated() {}
  virtual void OnDestroyed() {}
  // Return true (and set *result) to consume the message.
  virtual bool OnMessage(UINT message, WPARAM wparam, LPARAM lparam,
                         LRESULT* result);
  virtual uint32_t background_color() const { return 0xFFFFFFFF; }
  // Paints below the widgets (default: solid background).
  virtual void OnPaintBackground(RenderContext& context);
  // Paints above the widgets (default: nothing).
  virtual void OnPaintOverlay(RenderContext& context) { (void)context; }
  // Called after the window moved to a monitor with a different DPI (the
  // suggested outer rect has already been applied). Subclasses with
  // content-derived sizes should re-run their layout here.
  virtual void OnDpiChanged() {}

  const std::vector<std::unique_ptr<Widget>>& widgets() const {
    return widgets_;
  }

 private:
  static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wparam,
                                       LPARAM lparam);
  LRESULT WndProc(UINT message, WPARAM wparam, LPARAM lparam);
  void Paint();
  static void EnsureWindowClass();
  // Converts a mouse-message LPARAM (physical client px) to logical units.
  Point ToLogicalPoint(LPARAM lparam) const;

  HWND hwnd_ = nullptr;
  float scale_ = 1.0f;
  RenderContext render_context_;
  EventDispatcher dispatcher_;
  std::vector<std::unique_ptr<Widget>> widgets_;
  bool tracking_mouse_leave_ = false;
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_WINDOW_H_
