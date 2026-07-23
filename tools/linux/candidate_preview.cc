// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Offscreen preview of the Linux candidate window — renders PNGs with a
// plain cairo image surface, so it needs no display, no ibus and no
// install (works in CI/containers; the Linux twin of
// tools/macos/panel_preview.mm). Loads the repo dictionary, types a
// prefix through a real InputSession and snapshots the panel in
// light/dark mode and across pages.
//
// Build (from the repo root, after a normal cmake build):
//   cmake --build build --target paiza_candidate_preview
// Run:
//   OUT_DIR=/tmp PAIZA_INPUT=sa ./build/bin/paiza_candidate_preview
// Writes: panel_light.png, panel_dark.png, panel_page2.png,
//         panel_spine.png (debug stem overlay) in OUT_DIR.

#include <cairo.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "candidate_renderer.h"
#include "common/string_utils.h"
#include "core/candidate_engine.h"
#include "core/input_session.h"
#include "core/key_mapping_store.h"
#include "core/local_dictionary_source.h"

namespace {

using paiza::linux_ime::BuildPanelModel;
using paiza::linux_ime::PanelHover;
using paiza::linux_ime::PanelModel;
using paiza::linux_ime::RenderPanel;

// Renders at 2x so stem alignment and stroke quality are inspectable at
// the zoom level a review actually happens at.
constexpr double kScale = 2.0;

void SavePNG(const PanelModel& model, bool dark, bool debug_spine,
             const std::string& path) {
  const int width = static_cast<int>(model.width * kScale + 0.5);
  const int height = static_cast<int>(model.height * kScale + 0.5);
  cairo_surface_t* surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cairo_t* cr = cairo_create(surface);
  cairo_scale(cr, kScale, kScale);
  RenderPanel(cr, model, dark, PanelHover{}, debug_spine);
  cairo_destroy(cr);
  const cairo_status_t status =
      cairo_surface_write_to_png(surface, path.c_str());
  cairo_surface_destroy(surface);
  std::printf("wrote %s (%d x %d): %s\n", path.c_str(), width, height,
              cairo_status_to_string(status));
}

}  // namespace

int main() {
  const std::string out_dir = std::getenv("OUT_DIR") ?: ".";
  const std::string input = std::getenv("PAIZA_INPUT") ?: "sa";
  const std::string dictionary_path =
      std::getenv("PAIZA_DICT") ?: "data/dictionary.txt";
  const std::string font =
      std::getenv("PAIZA_FONT") ?: "Noto Sans Mongolian";

  auto dictionary = std::make_unique<paiza::LocalDictionarySource>();
  const size_t loaded =
      dictionary->LoadFromFile(paiza::Utf8ToWide(dictionary_path));
  std::printf("dictionary %s: %zu entries\n", dictionary_path.c_str(),
              loaded);

  paiza::CandidateEngine engine;
  engine.AddSource(std::move(dictionary));
  paiza::KeyMappingStore mappings;  // Real keymap.txt (hot-reloaded).
  paiza::InputSession session(&engine, &mappings);
  for (const char c : input) session.HandleLetter(c);
  std::printf("typed '%s': %zu candidates, %zu page(s)\n", input.c_str(),
              session.candidates().size(), session.page_count());

  PanelModel model = BuildPanelModel(&session, font);
  std::printf("font family resolved to '%s'\n", model.font_family.c_str());
  SavePNG(model, /*dark=*/false, /*debug_spine=*/false,
          out_dir + "/panel_light.png");
  SavePNG(model, /*dark=*/true, /*debug_spine=*/false,
          out_dir + "/panel_dark.png");
  SavePNG(model, /*dark=*/false, /*debug_spine=*/true,
          out_dir + "/panel_spine.png");

  if (session.PageForward()) {
    model = BuildPanelModel(&session, font);
    SavePNG(model, /*dark=*/false, /*debug_spine=*/false,
            out_dir + "/panel_page2.png");
  }
  return 0;
}
