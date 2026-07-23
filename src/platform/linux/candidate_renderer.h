// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Cairo/Pango drawing core of the Linux vertical candidate window — the
// port of the macOS candidate_panel.mm drawing (same layout constants,
// palette and stem-centring math, so all three platforms read as one
// product). Deliberately free of GTK window code: the GTK popup
// (candidate_window.cc) and the offscreen PNG preview tool
// (tools/linux/candidate_preview.cc) both draw through this file, which
// is what makes the visuals iterable headlessly in CI/containers.

#ifndef PAIZA_LINUX_CANDIDATE_RENDERER_H_
#define PAIZA_LINUX_CANDIDATE_RENDERER_H_

#include <cairo.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace paiza {
class InputSession;
}

namespace paiza::linux_ime {

// One candidate column of the current page (widths in logical pixels).
struct PanelColumn {
  std::string number;   // "1".."9", drawn above the word.
  std::string text;     // UTF-8 Mongolian word.
  size_t global_index;  // Index into session->candidates().
  bool selected;
  double width;        // Hugging width incl. side padding.
  double word_length;  // Shaped run length (down the column).
  double ascent;       // Horizontal-layout metrics of the shaped word.
  double descent;
};

// Everything RenderPanel needs; built from a live InputSession by
// BuildPanelModel and cheap to copy around.
struct PanelModel {
  std::vector<PanelColumn> columns;
  std::string page_label;  // "2/5"; empty on a single page.
  bool footer_visible = false;
  bool can_page_backward = false;
  bool can_page_forward = false;
  double text_band_height = 150;
  double width = 0;   // Overall panel size, from the columns + footer.
  double height = 0;
  std::string font_family;  // Resolved family (fontstems.txt lookup key).
  double font_size = 16;
};

// Live mouse state, drawn as hover feedback (unused by the preview).
struct PanelHover {
  size_t hovered_index = SIZE_MAX;  // Global candidate index.
  int hovered_button = 0;           // 0 none, 1 prev, 2 next.
};

// Measures the current page of `session` with the same Pango shaping used
// for painting (columns hug their glyphs; long words widen the band).
PanelModel BuildPanelModel(paiza::InputSession* session,
                           const std::string& font_family,
                           double font_size = 16.0);

// Draws the full panel (card, columns, footer) into `cr`, whose user
// space must already be in logical pixels with the origin at the panel's
// top-left. `debug_spine` overlays the per-column stem reference line.
void RenderPanel(cairo_t* cr, const PanelModel& model, bool dark,
                 const PanelHover& hover, bool debug_spine = false);

// Hit tests in panel coordinates, for the GTK window's mouse handling.
size_t HitTestColumn(const PanelModel& model, double x, double y);
int HitTestButton(const PanelModel& model, double x, double y);

}  // namespace paiza::linux_ime

#endif  // PAIZA_LINUX_CANDIDATE_RENDERER_H_
