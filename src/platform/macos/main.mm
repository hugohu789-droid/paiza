// Copyright 2026 Paiza. All rights reserved.
//
// Entry point of the macOS input method (.app bundle installed under
// ~/Library/Input Methods). InputMethodKit connects host applications to
// PaizaInputController through the IMKServer created here; the connection
// name must match InputMethodConnectionName in Info.plist.

#import <Cocoa/Cocoa.h>
#import <InputMethodKit/InputMethodKit.h>

int main(int argc, const char* argv[]) {
  (void)argc;
  (void)argv;
  @autoreleasepool {
    NSString* connection_name = [[NSBundle mainBundle]
        objectForInfoDictionaryKey:@"InputMethodConnectionName"];
    NSString* bundle_id = [[NSBundle mainBundle] bundleIdentifier];
    IMKServer* server =
        [[IMKServer alloc] initWithName:connection_name
                       bundleIdentifier:bundle_id];
    if (server == nil) {
      NSLog(@"PaizaIME: failed to create IMKServer (%@ / %@)",
            connection_name, bundle_id);
      return 1;
    }
    [[NSApplication sharedApplication] run];
  }
  return 0;
}
