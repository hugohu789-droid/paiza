// Copyright 2026 Paiza. All rights reserved.
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
