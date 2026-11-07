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
//
// With PAIZA_PROFILES (a directory) and PAIZA_PROFILE (an id) it assembles
// the sources the way the front-ends do -- including starting the profile's
// external engine, if it declares one -- rather than using the plain-text
// dictionary alone. That is what makes an engine profile's panel renderable
// here at all, and it is the Linux half of the same check the Windows
// service's --preview-candidates does:
//
//   PAIZA_PROFILES=data/profiles PAIZA_PROFILE=zcode PAIZA_INPUT=bagsi \
//   PAIZA_FONT="Z52 Hara Tig" OUT_DIR=/tmp ./build/bin/paiza_candidate_preview
//
// The engine executable is resolved inside PAIZA_PROFILES, exactly as the
// host resolves it inside the install directory.

#include <cairo.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "candidate_renderer.h"
#include "common/string_utils.h"
#include "core/candidate_engine.h"
#include "core/external_engine_source.h"
#include "core/input_profile.h"
#include "core/input_session.h"
#include "core/key_mapping_store.h"
#include "core/local_dictionary_source.h"
#include "core/profile_switch.h"

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
  std::string font = std::getenv("PAIZA_FONT") ?: "Noto Sans Mongolian";
  const std::string profiles_dir = std::getenv("PAIZA_PROFILES") ?: "";
  const std::string profile_id = std::getenv("PAIZA_PROFILE") ?: "";

  auto dictionary = std::make_unique<paiza::LocalDictionarySource>();
  const size_t loaded =
      dictionary->LoadFromFile(paiza::Utf8ToWide(dictionary_path));
  std::printf("dictionary %s: %zu entries\n", dictionary_path.c_str(),
              loaded);
  paiza::LocalDictionarySource* dictionary_ptr = dictionary.get();

  paiza::CandidateEngine engine;
  engine.AddSource(std::move(dictionary));

  // The same assembly the front-ends do: load the profiles, start the one
  // engine this profile declares, and gate every source through
  // ProfileSwitch so that a source answering when it should not is visible
  // here too.
  paiza::InputProfileRegistry profiles;
  std::unique_ptr<paiza::ProfileSwitch> profile_switch;
  std::unique_ptr<paiza::ExternalEngineSource> owned_engine_source;
  if (!profiles_dir.empty() && !profile_id.empty()) {
    const int count =
        profiles.LoadFromDirectory(paiza::Utf8ToWide(profiles_dir));
    const paiza::InputProfile* profile = profiles.Find(profile_id);
    std::printf("profiles %s: %d loaded, '%s' %s\n", profiles_dir.c_str(),
                count, profile_id.c_str(),
                profile == nullptr ? "NOT FOUND" : "found");
    if (profile == nullptr) return 1;
    if (!profile->font.empty()) font = paiza::WideToUtf8(profile->font);

    std::string reason;
    owned_engine_source = paiza::MakeEngineSource(
        *profile, paiza::Utf8ToWide(profiles_dir), &reason);
    if (owned_engine_source == nullptr && !reason.empty()) {
      std::printf("engine unavailable: %s\n", reason.c_str());
      return 1;
    }
    paiza::ExternalEngineSource* engine_ptr = owned_engine_source.get();
    if (engine_ptr != nullptr) {
      std::printf("engine '%s': mode '%s', scored %s\n",
                  paiza::WideToUtf8(profile->engine).c_str(),
                  profile->engine_mode.empty() ? paiza::kDefaultEngineMode
                                               : profile->engine_mode.c_str(),
                  profile->engine_scored ? "yes" : "no");
      engine.AddSource(std::move(owned_engine_source));
    }

    profile_switch =
        std::make_unique<paiza::ProfileSwitch>(&profiles, /*session=*/nullptr);
    for (const paiza::InputProfile& candidate : profiles.All()) {
      paiza::ProfileBinding binding;
      if (candidate.id == paiza::kDefaultProfileId) {
        binding.local_dictionary = dictionary_ptr;
      }
      if (candidate.id == profile->id) binding.engine_source = engine_ptr;
      profile_switch->Bind(candidate.id, binding);
    }
    profile_switch->Activate(profile->id);
  }

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
