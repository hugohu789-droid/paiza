// Copyright 2026 Paiza. All rights reserved.

#include "candidate_renderer.h"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include "common/string_utils.h"
#include "core/font_stem_config.h"
#include "core/input_session.h"

namespace paiza::linux_ime {
namespace {

// Layout constants mirror the Windows CandidateWindow/CandidateListView
// and the macOS panel so all platforms read as the same product
// (logical pixels; the candidate font size is absolute, not points, so
// the stem math below shares its unit).
constexpr double kPadding = 6.0;
constexpr double kNumberBandHeight = 15.0;
constexpr double kMinColumnWidth = 20.0;
constexpr double kMaxTextBandHeight = 340.0;
// Tighter than the Windows/macOS 3.0: stem-centred columns already
// reserve symmetric space around the stem, which reads as extra air
// between words on Linux (user feedback, 2026-07-10).
constexpr double kColumnSidePadding = 1.5;
constexpr double kTextBandPadding = 4.0;
constexpr double kFooterHeight = 24.0;
constexpr double kPageButtonWidth = 22.0;
constexpr double kPageButtonHeight = 18.0;
constexpr double kPageLabelWidth = 40.0;
constexpr double kCornerRadius = 8.0;
constexpr double kNumberFontSize = 9.5;

struct Color {
  double r, g, b;
};

constexpr Color FromRGB(uint32_t rgb) {
  return {((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0,
          (rgb & 0xFF) / 255.0};
}

// The Windows premium palette + its dark variant (same values as the
// macOS panel).
struct Palette {
  Color window_bg;
  Color border;
  Color number;
  Color number_selected;
  Color text;
  Color text_selected;
  Color selected_bg;
  Color hover_bg;
  Color footer_bg;
  Color separator;
  Color page_label;
  Color button_border;
  Color button_glyph;
  Color button_glyph_disabled;
  Color button_hover_bg;
};

constexpr Palette kLightPalette = {
    .window_bg = FromRGB(0xFFFFFF),
    .border = FromRGB(0xE2E4E8),
    .number = FromRGB(0xAAB0B8),
    .number_selected = FromRGB(0x2563EB),
    .text = FromRGB(0x1F2430),
    .text_selected = FromRGB(0x16305C),
    .selected_bg = FromRGB(0xEAF1FE),
    .hover_bg = FromRGB(0xF3F5F8),
    .footer_bg = FromRGB(0xFBFBFC),
    .separator = FromRGB(0xEDEEF1),
    .page_label = FromRGB(0xAAB0B8),
    .button_border = FromRGB(0xE2E4E8),
    .button_glyph = FromRGB(0x5F6368),
    .button_glyph_disabled = FromRGB(0xC6CAD0),
    .button_hover_bg = FromRGB(0xF3F5F8),
};

constexpr Palette kDarkPalette = {
    .window_bg = FromRGB(0x24272D),
    .border = FromRGB(0x3B3F46),
    .number = FromRGB(0x80868F),
    .number_selected = FromRGB(0x7AA5FF),
    .text = FromRGB(0xE8EAED),
    .text_selected = FromRGB(0xFFFFFF),
    .selected_bg = FromRGB(0x2C3D5C),
    .hover_bg = FromRGB(0x2D3138),
    .footer_bg = FromRGB(0x212429),
    .separator = FromRGB(0x33373E),
    .page_label = FromRGB(0x80868F),
    .button_border = FromRGB(0x3B3F46),
    .button_glyph = FromRGB(0xB6BBC2),
    .button_glyph_disabled = FromRGB(0x565B63),
    .button_hover_bg = FromRGB(0x2D3138),
};

void SetColor(cairo_t* cr, const Color& color) {
  cairo_set_source_rgb(cr, color.r, color.g, color.b);
}

void RoundedRect(cairo_t* cr, double x, double y, double w, double h,
                 double radius) {
  const double r = std::min({radius, w / 2, h / 2});
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -G_PI_2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI_2);
  cairo_arc(cr, x + r, y + h - r, r, G_PI_2, G_PI);
  cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI_2);
  cairo_close_path(cr);
}

PangoFontDescription* CandidateFontDescription(const PanelModel& model) {
  PangoFontDescription* desc = pango_font_description_new();
  pango_font_description_set_family(desc, model.font_family.c_str());
  // Absolute size: glyph units == drawing units, which the stem-ratio
  // offset (font_size * ratio) relies on.
  pango_font_description_set_absolute_size(desc,
                                           model.font_size * PANGO_SCALE);
  return desc;
}

PangoFontDescription* UiFontDescription(double size, PangoWeight weight) {
  PangoFontDescription* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Sans");
  pango_font_description_set_absolute_size(desc, size * PANGO_SCALE);
  pango_font_description_set_weight(desc, weight);
  return desc;
}

struct WordMetrics {
  double length;
  double ascent;
  double descent;
};

WordMetrics MeasureWord(PangoLayout* layout, const std::string& text) {
  pango_layout_set_text(layout, text.c_str(), -1);
  PangoLayoutLine* line = pango_layout_get_line_readonly(layout, 0);
  if (line == nullptr) return {0, 0, 0};
  PangoRectangle logical;
  pango_layout_line_get_extents(line, nullptr, &logical);
  // logical.y is the negated ascent relative to the baseline.
  return {static_cast<double>(logical.width) / PANGO_SCALE,
          static_cast<double>(-logical.y) / PANGO_SCALE,
          static_cast<double>(logical.y + logical.height) / PANGO_SCALE};
}

// Geometry (shared by drawing and hit testing) -------------------------------

struct Rect {
  double x, y, w, h;
  bool Contains(double px, double py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

double ColumnsTotalWidth(const PanelModel& model) {
  double total = 0;
  for (const PanelColumn& column : model.columns) total += column.width;
  return total;
}

double ColumnsOriginX(const PanelModel& model) {
  // Centre the candidate strip when the footer forces extra width.
  return std::max(kPadding, (model.width - ColumnsTotalWidth(model)) / 2);
}

Rect ColumnRect(const PanelModel& model, size_t position) {
  double x = ColumnsOriginX(model);
  for (size_t i = 0; i < position && i < model.columns.size(); ++i) {
    x += model.columns[i].width;
  }
  return {x, kPadding, model.columns[position].width,
          kNumberBandHeight + model.text_band_height};
}

Rect PrevButtonRect(const PanelModel& model) {
  const double footer_y = model.height - kFooterHeight;
  const double button_y = footer_y + (kFooterHeight - kPageButtonHeight) / 2;
  return {model.width - kPadding - 2 * kPageButtonWidth - 4, button_y,
          kPageButtonWidth, kPageButtonHeight};
}

Rect NextButtonRect(const PanelModel& model) {
  const double footer_y = model.height - kFooterHeight;
  const double button_y = footer_y + (kFooterHeight - kPageButtonHeight) / 2;
  return {model.width - kPadding - kPageButtonWidth, button_y,
          kPageButtonWidth, kPageButtonHeight};
}

Rect PageLabelRect(const PanelModel& model) {
  const Rect prev = PrevButtonRect(model);
  return {prev.x - 4 - kPageLabelWidth, prev.y, kPageLabelWidth,
          kPageButtonHeight};
}

// Drawing --------------------------------------------------------------------

// Draws horizontal `text` centred in `rect` with `desc` (numbers, page
// label, button glyphs).
void DrawCentredText(cairo_t* cr, const std::string& text,
                     PangoFontDescription* desc, const Rect& rect,
                     const Color& color) {
  PangoLayout* layout = pango_cairo_create_layout(cr);
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, text.c_str(), -1);
  int width = 0;
  int height = 0;
  pango_layout_get_pixel_size(layout, &width, &height);
  SetColor(cr, color);
  cairo_move_to(cr, rect.x + (rect.w - width) / 2,
                rect.y + (rect.h - height) / 2);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);
}

// Draws the word rotated 90° clockwise so it reads top-to-bottom, centred
// on the Mongolian stem when the font has a fontstems.txt calibration
// entry (otherwise centred on the glyph box). `rect` is the column's text
// band.
void DrawVerticalWord(cairo_t* cr, const PanelModel& model,
                      const PanelColumn& column,
                      PangoFontDescription* candidate_font, const Rect& rect,
                      const Color& color, bool debug_spine) {
  PangoLayout* layout = pango_cairo_create_layout(cr);
  pango_layout_set_font_description(layout, candidate_font);
  pango_layout_set_text(layout, column.text.c_str(), -1);
  PangoLayoutLine* line = pango_layout_get_line_readonly(layout, 0);
  if (line == nullptr) {
    g_object_unref(layout);
    return;
  }

  const double cx = rect.x + rect.w / 2;
  // After the rotation the column box spans [base - descent, base + ascent]
  // where `base` is the x position of the horizontal baseline. Stem-centred
  // fonts put the stem `font_size * ratio` right of the box's leading
  // (left) edge; without calibration the box itself is centred. Same math
  // as the Windows and macOS renderers.
  const std::optional<float> ratio =
      paiza::GetFontStemRatio(paiza::Utf8ToWide(model.font_family));
  double base_x;
  if (ratio.has_value()) {
    base_x = cx - model.font_size * *ratio + column.descent;
  } else {
    base_x = cx + (column.descent - column.ascent) / 2;
  }

  cairo_save(cr);
  // Maps shaped text space (x = flow, -y = ascent side) into the panel:
  // flow runs down the column, the ascent side faces right — the
  // 90°-clockwise rotation vertical Mongolian requires. Determinant +1,
  // so glyphs never mirror.
  cairo_matrix_t matrix;
  cairo_matrix_init(&matrix, /*xx=*/0, /*yx=*/1, /*xy=*/-1, /*yy=*/0,
                    /*x0=*/base_x, /*y0=*/rect.y);
  cairo_transform(cr, &matrix);
  SetColor(cr, color);
  cairo_move_to(cr, 0, 0);
  pango_cairo_show_layout_line(cr, line);
  cairo_restore(cr);
  g_object_unref(layout);

  if (debug_spine) {
    // Reference line down the column centre: every character's stem must
    // land on (or a constant distance from) this line.
    cairo_set_source_rgba(cr, 0.88, 0.08, 0.08, 0.75);
    cairo_rectangle(cr, cx, rect.y, 1, rect.h);
    cairo_fill(cr);
  }
}

void DrawPageButton(cairo_t* cr, const Rect& rect, const std::string& glyph,
                    bool enabled, bool hovered, const Palette& palette) {
  RoundedRect(cr, rect.x + 0.5, rect.y + 0.5, rect.w - 1, rect.h - 1, 4);
  if (enabled && hovered) {
    SetColor(cr, palette.button_hover_bg);
    cairo_fill_preserve(cr);
  }
  SetColor(cr, palette.button_border);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  PangoFontDescription* font = UiFontDescription(12, PANGO_WEIGHT_MEDIUM);
  DrawCentredText(cr, glyph, font, rect,
                  enabled ? palette.button_glyph
                          : palette.button_glyph_disabled);
  pango_font_description_free(font);
}

}  // namespace

PanelModel BuildPanelModel(paiza::InputSession* session,
                           const std::string& font_family, double font_size) {
  PanelModel model;
  model.font_size = font_size;
  model.font_family = font_family;

  // A tiny throwaway surface provides the Pango context for measuring;
  // shaping is identical to painting (same font map + description).
  cairo_surface_t* surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t* cr = cairo_create(surface);
  PangoLayout* layout = pango_cairo_create_layout(cr);
  PangoFontDescription* desc = CandidateFontDescription(model);
  pango_layout_set_font_description(layout, desc);

  // Resolve the family Pango actually picked (missing fonts fall back),
  // so the fontstems.txt lookup matches what is drawn.
  PangoContext* context = pango_layout_get_context(layout);
  PangoFont* loaded = pango_context_load_font(context, desc);
  if (loaded != nullptr) {
    PangoFontDescription* described = pango_font_describe(loaded);
    const char* family = pango_font_description_get_family(described);
    if (family != nullptr && *family != '\0') model.font_family = family;
    pango_font_description_free(described);
    g_object_unref(loaded);
  }
  const std::optional<float> ratio =
      paiza::GetFontStemRatio(paiza::Utf8ToWide(model.font_family));

  size_t first = 0;
  size_t last = 0;
  session->PageRange(&first, &last);
  double longest_word = 48;
  for (size_t i = first; i < last; ++i) {
    PanelColumn column;
    column.number = std::to_string(i - first + 1);
    column.text = paiza::WideToUtf8(session->candidates()[i].text);
    column.global_index = i;
    column.selected = i == session->selected_index();

    // Measure with the same shaping used for painting so long words are
    // never truncated; each column hugs its own glyph thickness.
    const WordMetrics metrics = MeasureWord(layout, column.text);
    column.word_length = metrics.length;
    column.ascent = metrics.ascent;
    column.descent = metrics.descent;
    double glyph_thickness = metrics.ascent + metrics.descent;
    if (ratio.has_value()) {
      // Stem-centred display shifts the glyph box inside the column, so
      // widen to twice the larger stem-side extent to avoid clipping.
      const double stem = font_size * *ratio;
      glyph_thickness = 2 * std::max(stem, glyph_thickness - stem);
    }
    column.width = std::max(kMinColumnWidth,
                            std::ceil(glyph_thickness) +
                                2 * kColumnSidePadding);
    longest_word = std::max(longest_word, metrics.length);
    model.columns.push_back(column);
  }

  pango_font_description_free(desc);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);

  const size_t pages = session->page_count();
  model.footer_visible = pages > 1;
  model.can_page_backward = session->current_page() > 0;
  model.can_page_forward = session->current_page() + 1 < pages;
  model.page_label =
      pages > 1 ? std::to_string(session->current_page() + 1) + "/" +
                      std::to_string(pages)
                : "";
  model.text_band_height =
      std::min(longest_word + kTextBandPadding, kMaxTextBandHeight);

  // The paging footer only appears with more than one page, so a single
  // page stays as tight as possible (same rule as Windows/macOS).
  const double footer_min_width =
      kPageLabelWidth + 2 * kPageButtonWidth + 12 + 2 * kPadding;
  double width = ColumnsTotalWidth(model) + 2 * kPadding;
  if (model.footer_visible) width = std::max(width, footer_min_width);
  model.width = width;
  model.height = kPadding + kNumberBandHeight + model.text_band_height +
                 kPadding + (model.footer_visible ? kFooterHeight : 0);
  return model;
}

void RenderPanel(cairo_t* cr, const PanelModel& model, bool dark,
                 const PanelHover& hover, bool debug_spine) {
  const Palette& palette = dark ? kDarkPalette : kLightPalette;

  // Rounded card: fill, footer strip, hairline border.
  RoundedRect(cr, 0.5, 0.5, model.width - 1, model.height - 1,
              kCornerRadius);
  SetColor(cr, palette.window_bg);
  cairo_fill_preserve(cr);

  if (model.footer_visible) {
    cairo_save(cr);
    cairo_clip(cr);
    const double footer_y = model.height - kFooterHeight;
    SetColor(cr, palette.footer_bg);
    cairo_rectangle(cr, 0, footer_y, model.width, kFooterHeight);
    cairo_fill(cr);
    SetColor(cr, palette.separator);
    cairo_rectangle(cr, 0, footer_y, model.width, 1);
    cairo_fill(cr);
    cairo_restore(cr);
  } else {
    cairo_new_path(cr);
  }

  // Columns: pill highlight, number band, vertical word.
  PangoFontDescription* candidate_font = CandidateFontDescription(model);
  PangoFontDescription* number_font =
      UiFontDescription(kNumberFontSize, PANGO_WEIGHT_NORMAL);
  PangoFontDescription* number_font_bold =
      UiFontDescription(kNumberFontSize, PANGO_WEIGHT_SEMIBOLD);
  for (size_t i = 0; i < model.columns.size(); ++i) {
    const PanelColumn& column = model.columns[i];
    const Rect column_rect = ColumnRect(model, i);

    // A slim rounded pill hugs the column; the accent tint reads as a
    // premium highlight rather than a heavy block (same as Windows).
    if (column.selected || column.global_index == hover.hovered_index) {
      SetColor(cr, column.selected ? palette.selected_bg : palette.hover_bg);
      RoundedRect(cr, column_rect.x + 3, column_rect.y + 1,
                  column_rect.w - 6, column_rect.h - 2, 7);
      cairo_fill(cr);
    }

    // Number above the candidate text; accent + semibold when selected.
    DrawCentredText(cr, column.number,
                    column.selected ? number_font_bold : number_font,
                    {column_rect.x, column_rect.y, column_rect.w,
                     kNumberBandHeight},
                    column.selected ? palette.number_selected
                                    : palette.number);

    const Rect text_rect = {column_rect.x,
                            column_rect.y + kNumberBandHeight, column_rect.w,
                            model.text_band_height};
    DrawVerticalWord(cr, model, column, candidate_font, text_rect,
                     column.selected ? palette.text_selected : palette.text,
                     debug_spine);
  }
  pango_font_description_free(number_font);
  pango_font_description_free(number_font_bold);
  pango_font_description_free(candidate_font);

  // Footer, bottom-right: [1/3] ‹ ›
  if (model.footer_visible) {
    PangoFontDescription* label_font =
        UiFontDescription(10.5, PANGO_WEIGHT_NORMAL);
    DrawCentredText(cr, model.page_label, label_font, PageLabelRect(model),
                    palette.page_label);
    pango_font_description_free(label_font);

    DrawPageButton(cr, PrevButtonRect(model), "‹",
                   model.can_page_backward, hover.hovered_button == 1,
                   palette);
    DrawPageButton(cr, NextButtonRect(model), "›",
                   model.can_page_forward, hover.hovered_button == 2,
                   palette);
  }

  RoundedRect(cr, 0.5, 0.5, model.width - 1, model.height - 1,
              kCornerRadius);
  SetColor(cr, palette.border);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
}

size_t HitTestColumn(const PanelModel& model, double x, double y) {
  for (size_t i = 0; i < model.columns.size(); ++i) {
    if (ColumnRect(model, i).Contains(x, y)) {
      return model.columns[i].global_index;
    }
  }
  return SIZE_MAX;
}

int HitTestButton(const PanelModel& model, double x, double y) {
  if (!model.footer_visible) return 0;
  if (PrevButtonRect(model).Contains(x, y)) return 1;
  if (NextButtonRect(model).Contains(x, y)) return 2;
  return 0;
}

}  // namespace paiza::linux_ime
