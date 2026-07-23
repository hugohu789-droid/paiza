// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Candidate window for macOS: a non-activating borderless NSPanel that
// mirrors paiza::InputSession state with the same visual language as the
// Windows CandidateWindow — vertical Mongolian words laid out as columns
// flowing left to right, the selection number above each word, a rounded
// card look, and a footer with ‹ › paging buttons plus a page indicator
// when there is more than one page. Words are shaped horizontally with
// CoreText and rotated 90° clockwise per column; columns are centred on
// the Mongolian stem (NIRUGU) using the shared fontstems.txt calibration
// (core/font_stem_config).

#import <Cocoa/Cocoa.h>

#include <cstddef>

namespace paiza {
class InputSession;
}

@interface PaizaCandidatePanel : NSPanel

// Mouse interaction, forwarded to the controller (which owns the session
// and the client connection). `index` is the global candidate index.
@property(nonatomic, copy) void (^onCandidateChosen)(size_t index);
@property(nonatomic, copy) void (^onPageBackward)(void);
@property(nonatomic, copy) void (^onPageForward)(void);

- (instancetype)init;

// Font used for the Mongolian candidate text (user setting; no-op when
// unchanged). Takes effect on the next -updateFromSession:.
- (void)setCandidateFontFamily:(NSString*)family;

// Copies the current candidates/selection/page out of the session,
// re-measures every column of the current page and resizes the panel.
- (void)updateFromSession:(paiza::InputSession*)session;

// Positions the panel right of the caret rectangle (screen coordinates)
// and shows it without stealing focus — vertical Mongolian advances in
// columns to the right, so right-of-caret is the natural spot (same rule
// as Windows CandidateWindow::MoveNearCaret). A zero rect centres the
// panel on screen.
- (void)showNearRect:(NSRect)caretRect;

- (void)hidePanel;

@end
