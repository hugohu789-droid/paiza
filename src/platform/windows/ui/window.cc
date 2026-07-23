// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "ui/window.h"

#include <dwmapi.h>
#include <windowsx.h>

#include "common/logging.h"
#include "ui/dpi.h"

namespace paiza::ui {
namespace {

constexpr wchar_t kWindowClassName[] = L"PaizaUiWindow";

// DWMWA_WINDOW_CORNER_PREFERENCE / DWMWCP_ROUND; defined inline so the
// project builds with pre-Win11 SDKs. No-op on Windows 10.
constexpr DWORD kDwmCornerPreference = 33;
constexpr DWORD kDwmCornerRound = 2;

HINSTANCE ModuleInstance() {
  // Handle of the module containing this code (works for both the TSF DLL
  // and the service executable).
  HMODULE module = nullptr;
  ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ModuleInstance), &module);
  return module;
}

}  // namespace

Window::Window() = default;

Window::~Window() { Destroy(); }

void Window::EnsureWindowClass() {
  static bool registered = false;
  if (registered) return;
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  // CS_DROPSHADOW gives the frameless IME popups a soft system shadow for
  // depth (premium look); harmless for the framed settings window.
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
  wc.lpfnWndProc = &Window::WndProcThunk;
  wc.hInstance = ModuleInstance();
  wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kWindowClassName;
  // Registration can race harmlessly; ERROR_CLASS_ALREADY_EXISTS is fine.
  ::RegisterClassExW(&wc);
  registered = true;
}

bool Window::Create(const CreateParams& params) {
  if (hwnd_ != nullptr) return true;
  EnsureWindowClass();
  // Create under an explicit per-monitor-V2 context so the window renders
  // at native pixels even inside DPI-unaware host processes (fixes the
  // bitmap-stretch blur of the IME popups).
  ScopedPerMonitorDpi dpi_scope;
  hwnd_ = ::CreateWindowExW(
      params.ex_style, kWindowClassName, params.title.c_str(), params.style,
      params.bounds.x, params.bounds.y, params.bounds.width,
      params.bounds.height, params.parent, nullptr, ModuleInstance(), this);
  if (hwnd_ == nullptr) {
    Logf(::paiza::LogLevel::kError, L"CreateWindowEx failed: %lu",
         ::GetLastError());
    return false;
  }
  // Rounded corners on Windows 11 (silently ignored on Windows 10).
  DWORD corner = kDwmCornerRound;
  ::DwmSetWindowAttribute(hwnd_, kDwmCornerPreference, &corner,
                          sizeof(corner));

  const UINT dpi = ::GetDpiForWindow(hwnd_);
  scale_ = dpi > 0 ? dpi / 96.0f : 1.0f;
  render_context_.set_scale(scale_);

  render_context_.BindToWindow(hwnd_);
  dispatcher_.SetWidgets(&widgets_);
  OnCreated();
  return true;
}

void Window::Destroy() {
  if (hwnd_ != nullptr) {
    ::DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

void Window::Show() {
  if (hwnd_ != nullptr) ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void Window::Hide() {
  if (hwnd_ != nullptr) ::ShowWindow(hwnd_, SW_HIDE);
}

bool Window::IsVisible() const {
  return hwnd_ != nullptr && ::IsWindowVisible(hwnd_);
}

void Window::SetBounds(const Rect& bounds) {
  if (hwnd_ == nullptr) return;
  // Physical coordinates regardless of the host thread's DPI context.
  ScopedPerMonitorDpi dpi_scope;
  ::SetWindowPos(hwnd_, nullptr, bounds.x, bounds.y, bounds.width,
                 bounds.height, SWP_NOACTIVATE | SWP_NOZORDER);
}

Rect Window::GetBounds() const {
  RECT rc = {};
  if (hwnd_ != nullptr) {
    ScopedPerMonitorDpi dpi_scope;
    ::GetWindowRect(hwnd_, &rc);
  }
  return {rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top};
}

Size Window::logical_client_size() const {
  RECT rc = {};
  if (hwnd_ != nullptr) ::GetClientRect(hwnd_, &rc);
  return {static_cast<int>((rc.right - rc.left) / scale_ + 0.5f),
          static_cast<int>((rc.bottom - rc.top) / scale_ + 0.5f)};
}

void Window::Invalidate() {
  if (hwnd_ != nullptr) ::InvalidateRect(hwnd_, nullptr, FALSE);
}

// static
LRESULT CALLBACK Window::WndProcThunk(HWND hwnd, UINT message, WPARAM wparam,
                                      LPARAM lparam) {
  Window* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<Window*>(create->lpCreateParams);
    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = hwnd;
  } else {
    self = reinterpret_cast<Window*>(
        ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self == nullptr) {
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
  }
  return self->WndProc(message, wparam, lparam);
}

LRESULT Window::WndProc(UINT message, WPARAM wparam, LPARAM lparam) {
  LRESULT result = 0;
  if (OnMessage(message, wparam, lparam, &result)) return result;

  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT ps = {};
      ::BeginPaint(hwnd_, &ps);
      Paint();
      ::EndPaint(hwnd_, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;  // Direct2D repaints everything.
    case WM_SIZE:
      render_context_.OnResize(LOWORD(lparam), HIWORD(lparam));
      return 0;
    case WM_MOUSEACTIVATE:
      // IME popups (WS_EX_NOACTIVATE) must never steal focus from the
      // host application; regular windows (settings) activate normally.
      if ((::GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) & WS_EX_NOACTIVATE) !=
          0) {
        return MA_NOACTIVATE;
      }
      break;
    case WM_MOUSEMOVE: {
      if (!tracking_mouse_leave_) {
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd_, 0};
        ::TrackMouseEvent(&track);
        tracking_mouse_leave_ = true;
      }
      dispatcher_.OnMouseMove(ToLogicalPoint(lparam));
      return 0;
    }
    case WM_MOUSELEAVE:
      tracking_mouse_leave_ = false;
      dispatcher_.OnMouseLeaveWindow();
      return 0;
    case WM_LBUTTONDOWN:
      ::SetCapture(hwnd_);
      dispatcher_.OnMouseDown(ToLogicalPoint(lparam));
      return 0;
    case WM_LBUTTONUP:
      ::ReleaseCapture();
      dispatcher_.OnMouseUp(ToLogicalPoint(lparam));
      return 0;
    case WM_DPICHANGED: {
      scale_ = HIWORD(wparam) / 96.0f;
      render_context_.set_scale(scale_);
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      if (suggested != nullptr) {
        ::SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                       suggested->right - suggested->left,
                       suggested->bottom - suggested->top,
                       SWP_NOACTIVATE | SWP_NOZORDER);
      }
      OnDpiChanged();
      Invalidate();
      return 0;
    }
    case WM_NCDESTROY:
      hwnd_ = nullptr;
      dispatcher_.Reset();
      OnDestroyed();
      return 0;
    default:
      break;
  }
  return ::DefWindowProcW(hwnd_, message, wparam, lparam);
}

bool Window::OnMessage(UINT, WPARAM, LPARAM, LRESULT*) { return false; }

Point Window::ToLogicalPoint(LPARAM lparam) const {
  return {static_cast<int>(GET_X_LPARAM(lparam) / scale_),
          static_cast<int>(GET_Y_LPARAM(lparam) / scale_)};
}

void Window::OnPaintBackground(RenderContext& context) {
  context.Clear(background_color());
}

void Window::Paint() {
  if (!render_context_.BeginDraw()) return;
  OnPaintBackground(render_context_);
  for (const auto& widget : widgets_) {
    if (widget->visible()) widget->OnPaint(render_context_);
  }
  OnPaintOverlay(render_context_);
  if (!render_context_.EndDraw()) {
    // Device lost: schedule a repaint with fresh resources.
    Invalidate();
  }
}

}  // namespace paiza::ui
