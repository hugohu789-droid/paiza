// Copyright 2026 Paiza. All rights reserved.

#import <Foundation/Foundation.h>

#include "settings_store.h"

#include <algorithm>

#include "common/string_utils.h"

namespace paiza::mac {
namespace {

// Key names match the Windows registry value names so the two stores stay
// recognisably the same settings.
NSString* const kKeyCandidatesPerPage = @"CandidatesPerPage";
NSString* const kKeyCloudEnabled = @"CloudSourceEnabled";
NSString* const kKeyUiLanguage = @"UiLanguage";
NSString* const kKeyCandidateFont = @"CandidateFont";

}  // namespace

Settings SettingsStore::Load() {
  NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
  Settings settings;

  NSNumber* per_page = [defaults objectForKey:kKeyCandidatesPerPage];
  if (per_page != nil) {
    settings.candidates_per_page =
        std::clamp<uint32_t>(static_cast<uint32_t>(per_page.intValue), 1, 9);
  }
  settings.cloud_source_enabled = [defaults boolForKey:kKeyCloudEnabled];
  NSNumber* language = [defaults objectForKey:kKeyUiLanguage];
  settings.ui_language =
      language != nil
          ? UiLanguageFromValue(static_cast<uint32_t>(language.intValue))
          : DetectSystemLanguage();
  NSString* font = [defaults stringForKey:kKeyCandidateFont];
  if (font.length > 0) {
    settings.candidate_font = Utf8ToWide([font UTF8String]);
  }
  return settings;
}

void SettingsStore::Save(const Settings& settings) {
  NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
  [defaults setInteger:settings.candidates_per_page
                forKey:kKeyCandidatesPerPage];
  [defaults setBool:settings.cloud_source_enabled forKey:kKeyCloudEnabled];
  [defaults setInteger:static_cast<NSInteger>(settings.ui_language)
                forKey:kKeyUiLanguage];
  const std::string font = WideToUtf8(settings.candidate_font);
  [defaults setObject:[NSString stringWithUTF8String:font.c_str()] ?: @""
               forKey:kKeyCandidateFont];
}

}  // namespace paiza::mac
