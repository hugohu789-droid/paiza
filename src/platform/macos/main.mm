// Copyright 2026 Paiza. All rights reserved.
//
// Entry point of the macOS input method (.app bundle installed under
// ~/Library/Input Methods). InputMethodKit connects host applications to
// PaizaInputController through the IMKServer created here; the connection
// name must match InputMethodConnectionName in Info.plist.
//
// Settings entry points (parity with Windows "PaizaService --settings"):
//   * the "Paiza Settings…" item in the menu-bar input menu;
//   * re-opening the running app (double-click in Finder / `open -a`)
//     lands in the delegate's reopen handler below;
//   * a second instance (`open -n`, or IMKServer name already taken)
//     shows the settings window standalone and quits when it closes.

#import <Cocoa/Cocoa.h>
#import <InputMethodKit/InputMethodKit.h>

#include <cstring>
#include <unistd.h>

#include "common/logging.h"
#include "core/font_stem_config.h"

#import "settings_window.h"

@interface PaizaAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation PaizaAppDelegate {
  PaizaSettingsWindowController* _settings;
}

// Double-clicking the app while the IME is already running delivers a
// reopen event to this (server) instance: open the settings window.
- (BOOL)applicationShouldHandleReopen:(NSApplication*)application
                    hasVisibleWindows:(BOOL)hasVisibleWindows {
  (void)application;
  (void)hasVisibleWindows;
  PAIZA_LOG_INFO(L"reopen -> settings window");
  if (_settings == nil) {
    _settings = [[PaizaSettingsWindowController alloc]
        initWithOnChanged:^(const paiza::mac::Settings&) {
          // Controllers re-read NSUserDefaults on every sync; nothing to
          // push here.
        }];
  }
  [_settings showSettings];
  return NO;
}

@end

namespace {

// True when this launch should show the settings window instead of
// serving IMK: either an explicit --settings, or the IME is already
// running (second instance — mirrors "PaizaService.exe --settings" on
// Windows, where a second launch becomes the settings dialog).
bool IsSettingsLaunch(int argc, const char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--settings") == 0) return true;
  }
  NSArray<NSRunningApplication*>* instances = [NSRunningApplication
      runningApplicationsWithBundleIdentifier:[[NSBundle mainBundle]
                                                  bundleIdentifier]];
  for (NSRunningApplication* instance in instances) {
    if (instance.processIdentifier != getpid()) return true;
  }
  return false;
}

}  // namespace

int main(int argc, const char* argv[]) {
  @autoreleasepool {
    if (IsSettingsLaunch(argc, argv)) {
      PAIZA_LOG_INFO(L"settings launch -> standalone settings window");
      PaizaSettingsWindowController* settings =
          [[PaizaSettingsWindowController alloc]
              initWithOnChanged:^(const paiza::mac::Settings&){
              }];
      settings.onClosed = ^{
        [NSApp terminate:nil];
      };
      [settings showSettings];
      [[NSApplication sharedApplication] run];
      return 0;
    }

    NSString* connection_name = [[NSBundle mainBundle]
        objectForInfoDictionaryKey:@"InputMethodConnectionName"];
    NSString* bundle_id = [[NSBundle mainBundle] bundleIdentifier];

    // Owner process writes the calibration file so users can edit it
    // (same rule as the Windows service).
    paiza::EnsureFontStemConfigFile();

    IMKServer* server =
        [[IMKServer alloc] initWithName:connection_name
                       bundleIdentifier:bundle_id];
    if (server == nil) {
      NSLog(@"PaizaIME: failed to create IMKServer (%@ / %@)",
            connection_name, bundle_id);
      return 1;
    }

    PaizaAppDelegate* delegate = [[PaizaAppDelegate alloc] init];
    NSApplication* app = [NSApplication sharedApplication];
    app.delegate = delegate;
    PAIZA_LOG_INFO(L"IMKServer up");
    [app run];
  }
  return 0;
}
