// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// IMKInputController subclass: translates NSEvents into InputSession
// calls and mirrors the session state back into the client (marked text)
// and the candidate panel. All IME behaviour lives in paiza::InputSession
// (src/core) — keep this layer free of input logic so the platforms stay
// feature-identical.

#import <Cocoa/Cocoa.h>
#import <InputMethodKit/InputMethodKit.h>

#include "core/profile_switch.h"

namespace paiza::macos {

// The one ProfileSwitch this process has, built on first use. Exposed
// because the settings window lives in the same process and has to ask it
// why the active profile's candidate engine is not working -- it is the
// only thing that knows, and on this platform no file has to carry the
// answer across a process boundary as it does on Linux.
ProfileSwitch* SharedProfileSwitch();

}  // namespace paiza::macos

@interface PaizaInputController : IMKInputController
@end
