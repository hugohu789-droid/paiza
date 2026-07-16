// Copyright 2026 Paiza. All rights reserved.
//
// The macOS settings window — the equivalent of the Windows settings
// dialog (src/platform/windows/service/settings_window.h), reached from
// the input menu's "Paiza Settings…" item (which macOS also surfaces as
// the input source's "Open Keyboard Settings"). Exposes exactly the same
// options as Windows: candidates per page, cloud candidates, UI language,
// candidate window font, and the editable key mapping. Built with native
// AppKit controls (a regular, activatable window, unlike the IME popups)
// and localized through core/localization's UiStrings.

#import <Cocoa/Cocoa.h>

#include "settings_store.h"

namespace paiza {
class UserDictionary;
}

@interface PaizaSettingsWindowController : NSObject

// Fired when the window closes (Save or Close). The standalone settings
// instance (see main.mm) uses this to terminate itself.
@property(nonatomic, copy) void (^onClosed)(void);

// `onChanged` fires after the user saves; the controller re-applies the
// settings to the live session, candidate panel and engine.
// `userDictionary` (may be null) receives TXT imports live; when null the
// window imports into the shared user.db directly (standalone instance).
- (instancetype)initWithOnChanged:(void (^)(const paiza::mac::Settings&))
                                      onChanged
                   userDictionary:(paiza::UserDictionary*)userDictionary;

// Loads the current settings, (re)builds the window and shows it centred
// and focused. Safe to call repeatedly.
- (void)showSettings;

@end
