// Copyright 2026 Paiza. All rights reserved.
//
// Candidate window for macOS: a non-activating borderless NSPanel that
// mirrors paiza::InputSession state (numbers above vertical Mongolian
// words, page indicator). First iteration draws a simple horizontal list
// so the pipeline is visible end-to-end; the CoreText vertical layout
// with per-font stem centring (fontstems.txt, same calibration data as
// Windows) is the follow-up — see docs/macos-port.md.

#import <Cocoa/Cocoa.h>

namespace paiza {
class InputSession;
}

@interface PaizaCandidatePanel : NSPanel

- (instancetype)init;

// Copies the current candidates/selection/page out of the session.
- (void)updateFromSession:(paiza::InputSession*)session;

// Positions the panel next to the caret rectangle (screen coordinates)
// and shows it without stealing focus. A zero rect centres on screen.
- (void)showNearRect:(NSRect)caretRect;

- (void)hidePanel;

@end
