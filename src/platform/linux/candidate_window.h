// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// GTK popup hosting the vertical candidate panel (drawing lives in
// candidate_renderer.{h,cc}). Always runs on the X11 GDK backend: a
// GTK_WINDOW_POPUP is an override-redirect window we can place next to
// the caret without stealing focus. On GNOME Wayland that means
// XWayland — the one surface a background process can still position
// there (see Available() for the coordinate-space reasoning). Only
// when no X display exists at all (Wayland without XWayland, headless)
// does the engine keep the v1 system IBusLookupTable — check
// Available() first.

#ifndef PAIZA_LINUX_CANDIDATE_WINDOW_H_
#define PAIZA_LINUX_CANDIDATE_WINDOW_H_

#include <gtk/gtk.h>

#include <cstddef>
#include <functional>
#include <string>

#include "candidate_renderer.h"

namespace paiza {
class InputSession;
}

namespace paiza::linux_ime {

class CandidateWindow {
 public:
  // True when GTK could initialize on an X11 display. Safe to call
  // repeatedly; the first call runs gtk_init_check(). When false, do not
  // construct a CandidateWindow.
  static bool Available();

  CandidateWindow();
  ~CandidateWindow();

  CandidateWindow(const CandidateWindow&) = delete;
  CandidateWindow& operator=(const CandidateWindow&) = delete;

  // Candidate font family (from settings; UTF-8). Applies on the next
  // UpdateFromSession.
  void SetFont(const std::string& family);

  // Rebuilds the panel model from the session's current page and resizes
  // the window; call ShowNear afterwards to (re)position and map it.
  void UpdateFromSession(paiza::InputSession* session);

  // Places the panel right of the caret rectangle (root coordinates, as
  // delivered by IBus set-cursor-location), clamped to the monitor
  // workarea, and shows it.
  void ShowNear(int caret_x, int caret_y, int caret_w, int caret_h);
  void Hide();

  // Mouse interaction, reported with global candidate indices.
  std::function<void(size_t)> on_candidate_chosen;
  std::function<void()> on_page_backward;
  std::function<void()> on_page_forward;

 private:
  static gboolean OnDraw(GtkWidget* widget, cairo_t* cr, gpointer data);
  static gboolean OnButtonPress(GtkWidget* widget, GdkEventButton* event,
                                gpointer data);
  static gboolean OnMotion(GtkWidget* widget, GdkEventMotion* event,
                           gpointer data);
  static gboolean OnLeave(GtkWidget* widget, GdkEventCrossing* event,
                          gpointer data);

  bool IsDarkTheme() const;

  GtkWidget* window_ = nullptr;  // GTK_WINDOW_POPUP, owns area_.
  GtkWidget* area_ = nullptr;    // GtkDrawingArea.
  PanelModel model_;
  PanelHover hover_;
  std::string font_family_ = "Noto Sans Mongolian";
  bool debug_spine_ = false;  // PAIZA_DEBUG_SPINE=1 (Windows/macOS parity).
};

}  // namespace paiza::linux_ime

#endif  // PAIZA_LINUX_CANDIDATE_WINDOW_H_
