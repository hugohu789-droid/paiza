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

@interface PaizaInputController : IMKInputController
@end
