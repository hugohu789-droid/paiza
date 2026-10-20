// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

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
NSString* const kKeyEquivOu = @"EquivOU";
NSString* const kKeyEquivQv = @"EquivQV";
NSString* const kKeyEquivHg = @"EquivHG";
NSString* const kKeyEquivTd = @"EquivTD";
NSString* const kKeyEquivEw = @"EquivEW";
NSString* const kKeyActiveProfile = @"ActiveProfile";
NSString* const kKeyInlineComposition = @"InlineComposition";

// Reads a bool that defaults to true when the key was never written (an
// absent NSUserDefaults key reads as NO, which would flip the default).
bool LoadBoolOrTrue(NSUserDefaults* defaults, NSString* key) {
  NSNumber* value = [defaults objectForKey:key];
  return value != nil ? value.boolValue : true;
}

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
  settings.equiv_ou = LoadBoolOrTrue(defaults, kKeyEquivOu);
  settings.equiv_qv = LoadBoolOrTrue(defaults, kKeyEquivQv);
  settings.equiv_hg = LoadBoolOrTrue(defaults, kKeyEquivHg);
  settings.equiv_td = LoadBoolOrTrue(defaults, kKeyEquivTd);
  settings.equiv_ew = LoadBoolOrTrue(defaults, kKeyEquivEw);
  NSString* profile = [defaults stringForKey:kKeyActiveProfile];
  if (profile.length > 0) settings.active_profile = [profile UTF8String];
  NSNumber* inlineMode = [defaults objectForKey:kKeyInlineComposition];
  if (inlineMode != nil) {
    settings.inline_composition = paiza::InlineCompositionFromValue(
        static_cast<uint32_t>(inlineMode.intValue));
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
  [defaults setBool:settings.equiv_ou forKey:kKeyEquivOu];
  [defaults setBool:settings.equiv_qv forKey:kKeyEquivQv];
  [defaults setBool:settings.equiv_hg forKey:kKeyEquivHg];
  [defaults setBool:settings.equiv_td forKey:kKeyEquivTd];
  [defaults setBool:settings.equiv_ew forKey:kKeyEquivEw];
  [defaults setObject:[NSString stringWithUTF8String:
                           settings.active_profile.c_str()]
                           ?: @"unicode"
               forKey:kKeyActiveProfile];
  [defaults setInteger:static_cast<NSInteger>(settings.inline_composition)
                forKey:kKeyInlineComposition];
}

}  // namespace paiza::mac
