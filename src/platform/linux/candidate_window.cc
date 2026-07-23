// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "candidate_window.h"

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "core/input_session.h"

namespace paiza::linux_ime {

bool CandidateWindow::Available() {
  static const bool available = [] {
    // Wayland sessions never get the custom window. Positioning one
    // there would require XWayland, and that proved unreliable in
    // practice: on a QEMU Ubuntu VM, override-redirect XWayland windows
    // composite as solid black even for raw Xlib drawing (see
    // docs/linux-port.md "Wayland candidate rendering"). On Wayland the
    // engine keeps the v1 IBusLookupTable — GNOME Shell positions that
    // popup correctly for every client — and the bundled shell
    // extension (src/platform/linux/gnome-extension/) rotates its
    // candidates into traditional vertical Mongolian.
    if (g_getenv("WAYLAND_DISPLAY") != nullptr) return false;
    // Xorg session: pin the X11 backend. A stray GDK_BACKEND from the
    // environment would override gdk_set_allowed_backends and empty the
    // backend intersection, so set both. Safe: GTK is not initialized
    // yet.
    g_setenv("GDK_BACKEND", "x11", TRUE);
    gdk_set_allowed_backends("x11");
    // The engine process is spawned by ibus-daemon without a controlling
    // terminal; gtk_init_check (unlike gtk_init) survives a missing
    // display.
    if (!gtk_init_check(nullptr, nullptr)) return false;
#ifdef GDK_WINDOWING_X11
    GdkDisplay* display = gdk_display_get_default();
    return display != nullptr && GDK_IS_X11_DISPLAY(display);
#else
    return false;
#endif
  }();
  return available;
}

CandidateWindow::CandidateWindow() {
  const char* spine = std::getenv("PAIZA_DEBUG_SPINE");
  debug_spine_ = spine != nullptr && std::strcmp(spine, "1") == 0;

  window_ = gtk_window_new(GTK_WINDOW_POPUP);
  gtk_window_set_accept_focus(GTK_WINDOW(window_), FALSE);
  gtk_window_set_focus_on_map(GTK_WINDOW(window_), FALSE);
  gtk_window_set_type_hint(GTK_WINDOW(window_),
                           GDK_WINDOW_TYPE_HINT_POPUP_MENU);
  gtk_widget_set_app_paintable(window_, TRUE);
  // RGBA visual (when the compositor offers one) keeps the rounded card
  // corners transparent instead of black.
  GdkScreen* screen = gtk_widget_get_screen(window_);
  GdkVisual* rgba = gdk_screen_get_rgba_visual(screen);
  if (rgba != nullptr) gtk_widget_set_visual(window_, rgba);

  area_ = gtk_drawing_area_new();
  gtk_widget_add_events(area_, GDK_BUTTON_PRESS_MASK |
                                   GDK_POINTER_MOTION_MASK |
                                   GDK_LEAVE_NOTIFY_MASK);
  gtk_container_add(GTK_CONTAINER(window_), area_);

  g_signal_connect(area_, "draw", G_CALLBACK(OnDraw), this);
  g_signal_connect(area_, "button-press-event", G_CALLBACK(OnButtonPress),
                   this);
  g_signal_connect(area_, "motion-notify-event", G_CALLBACK(OnMotion), this);
  g_signal_connect(area_, "leave-notify-event", G_CALLBACK(OnLeave), this);
}

CandidateWindow::~CandidateWindow() {
  if (window_ != nullptr) gtk_widget_destroy(window_);
}

void CandidateWindow::SetFont(const std::string& family) {
  if (!family.empty()) font_family_ = family;
}

void CandidateWindow::UpdateFromSession(paiza::InputSession* session) {
  model_ = BuildPanelModel(session, font_family_);
  hover_ = PanelHover{};
  const int width = static_cast<int>(model_.width + 0.5);
  const int height = static_cast<int>(model_.height + 0.5);
  gtk_widget_set_size_request(area_, width, height);
  gtk_window_resize(GTK_WINDOW(window_), width, height);
  gtk_widget_queue_draw(area_);
}

void CandidateWindow::ShowNear(int caret_x, int caret_y, int caret_w,
                               int caret_h) {
  const int width = static_cast<int>(model_.width + 0.5);
  const int height = static_cast<int>(model_.height + 0.5);

  GdkDisplay* display = gtk_widget_get_display(window_);
  GdkMonitor* monitor =
      gdk_display_get_monitor_at_point(display, caret_x, caret_y);
  GdkRectangle work = {0, 0, 1920, 1080};
  if (monitor != nullptr) gdk_monitor_get_workarea(monitor, &work);

  // Right of the caret, top-aligned with the line: vertical Mongolian
  // advances in columns to the right. Flip to the left when the right
  // side has no room (same policy as the macOS panel).
  int x = caret_x + caret_w + 4;
  int y = caret_y;
  if (x + width > work.x + work.width) {
    x = caret_x - 4 - width;
    if (x < work.x) x = work.x + work.width - width;
  }
  x = std::max(x, work.x);
  y = std::clamp(y, work.y, work.y + work.height - height);
  (void)caret_h;

  gtk_window_move(GTK_WINDOW(window_), x, y);
  gtk_widget_show_all(window_);
}

void CandidateWindow::Hide() { gtk_widget_hide(window_); }

bool CandidateWindow::IsDarkTheme() const {
  GtkSettings* settings = gtk_settings_get_default();
  if (settings == nullptr) return false;
  gboolean prefer_dark = FALSE;
  gchar* theme = nullptr;
  g_object_get(settings, "gtk-application-prefer-dark-theme", &prefer_dark,
               "gtk-theme-name", &theme, nullptr);
  bool dark = prefer_dark == TRUE;
  if (!dark && theme != nullptr) {
    gchar* lowered = g_ascii_strdown(theme, -1);
    dark = std::strstr(lowered, "dark") != nullptr;
    g_free(lowered);
  }
  g_free(theme);
  return dark;
}

gboolean CandidateWindow::OnDraw(GtkWidget* widget, cairo_t* cr,
                                 gpointer data) {
  (void)widget;
  auto* self = static_cast<CandidateWindow*>(data);
  // Transparent base so the rounded card corners show whatever is behind
  // the popup (requires the RGBA visual; harmless without it).
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_restore(cr);
  RenderPanel(cr, self->model_, self->IsDarkTheme(), self->hover_,
              self->debug_spine_);
  return TRUE;
}

gboolean CandidateWindow::OnButtonPress(GtkWidget* widget,
                                        GdkEventButton* event,
                                        gpointer data) {
  (void)widget;
  auto* self = static_cast<CandidateWindow*>(data);
  if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
  const int button = HitTestButton(self->model_, event->x, event->y);
  if (button == 1) {
    if (self->model_.can_page_backward && self->on_page_backward) {
      self->on_page_backward();
    }
    return TRUE;
  }
  if (button == 2) {
    if (self->model_.can_page_forward && self->on_page_forward) {
      self->on_page_forward();
    }
    return TRUE;
  }
  const size_t index = HitTestColumn(self->model_, event->x, event->y);
  if (index != SIZE_MAX && self->on_candidate_chosen) {
    self->on_candidate_chosen(index);
  }
  return TRUE;
}

gboolean CandidateWindow::OnMotion(GtkWidget* widget, GdkEventMotion* event,
                                   gpointer data) {
  (void)widget;
  auto* self = static_cast<CandidateWindow*>(data);
  PanelHover hover;
  hover.hovered_index = HitTestColumn(self->model_, event->x, event->y);
  hover.hovered_button = HitTestButton(self->model_, event->x, event->y);
  if (hover.hovered_index != self->hover_.hovered_index ||
      hover.hovered_button != self->hover_.hovered_button) {
    self->hover_ = hover;
    gtk_widget_queue_draw(self->area_);
  }
  const bool clickable =
      hover.hovered_index != SIZE_MAX ||
      (hover.hovered_button == 1 && self->model_.can_page_backward) ||
      (hover.hovered_button == 2 && self->model_.can_page_forward);
  GdkWindow* gdk_window = gtk_widget_get_window(self->area_);
  if (gdk_window != nullptr) {
    GdkCursor* cursor =
        clickable ? gdk_cursor_new_from_name(
                        gtk_widget_get_display(self->area_), "pointer")
                  : nullptr;
    gdk_window_set_cursor(gdk_window, cursor);
    if (cursor != nullptr) g_object_unref(cursor);
  }
  return TRUE;
}

gboolean CandidateWindow::OnLeave(GtkWidget* widget, GdkEventCrossing* event,
                                  gpointer data) {
  (void)widget;
  (void)event;
  auto* self = static_cast<CandidateWindow*>(data);
  if (self->hover_.hovered_index != SIZE_MAX ||
      self->hover_.hovered_button != 0) {
    self->hover_ = PanelHover{};
    gtk_widget_queue_draw(self->area_);
  }
  return FALSE;
}

}  // namespace paiza::linux_ime
