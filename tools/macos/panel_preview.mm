// Copyright 2026 Paiza. All rights reserved.
//
// Offscreen preview of the macOS candidate panel — renders PNGs without
// installing the input method or granting any permissions. Loads the repo
// dictionary, types a prefix through a real InputSession and snapshots
// the panel in light/dark mode and across pages.
//
// Build (from the repo root, after a normal cmake build):
//   cmake --build build --target paiza_panel_preview
// Run:
//   OUT_DIR=/tmp PAIZA_INPUT=sa ./build/bin/paiza_panel_preview
// Writes: panel_light.png, panel_dark.png, panel_page2.png in OUT_DIR.

#import <Cocoa/Cocoa.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "common/string_utils.h"
#include "core/candidate_engine.h"
#include "core/input_session.h"
#include "core/key_mapping_store.h"
#include "core/local_dictionary_source.h"

#import "candidate_panel.h"

namespace {

void SavePNG(NSView* view, NSString* path) {
  NSBitmapImageRep* rep =
      [view bitmapImageRepForCachingDisplayInRect:view.bounds];
  [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
  NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG
                                  properties:@{}];
  [png writeToFile:path atomically:YES];
  NSLog(@"wrote %@ (%.0f x %.0f)", path, view.bounds.size.width,
        view.bounds.size.height);
}

}  // namespace

int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];

    NSString* out_dir =
        [NSString stringWithUTF8String:getenv("OUT_DIR") ?: "."];
    const std::string input = getenv("PAIZA_INPUT") ?: "sa";
    NSString* dictionary_path =
        [NSString stringWithUTF8String:getenv("PAIZA_DICT")
                                           ?: "data/dictionary.txt"];

    auto dictionary = std::make_unique<paiza::LocalDictionarySource>();
    const size_t loaded = dictionary->LoadFromFile(
        paiza::Utf8ToWide([dictionary_path UTF8String]));
    NSLog(@"dictionary %@: %zu entries", dictionary_path, loaded);

    paiza::CandidateEngine engine;
    engine.AddSource(std::move(dictionary));
    paiza::KeyMappingStore mappings;  // Real keymap.txt (hot-reloaded).
    paiza::InputSession session(&engine, &mappings);
    for (const char c : input) session.HandleLetter(c);
    NSLog(@"typed '%s': %zu candidates, %zu page(s)", input.c_str(),
          session.candidates().size(), session.page_count());

    PaizaCandidatePanel* panel = [[PaizaCandidatePanel alloc] init];

    panel.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    [panel updateFromSession:&session];
    SavePNG(panel.contentView,
            [out_dir stringByAppendingPathComponent:@"panel_light.png"]);

    panel.appearance =
        [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    [panel updateFromSession:&session];
    SavePNG(panel.contentView,
            [out_dir stringByAppendingPathComponent:@"panel_dark.png"]);

    if (session.PageForward()) {
      panel.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
      [panel updateFromSession:&session];
      SavePNG(panel.contentView,
              [out_dir stringByAppendingPathComponent:@"panel_page2.png"]);
    }
  }
  return 0;
}
