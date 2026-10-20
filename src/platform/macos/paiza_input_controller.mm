// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#import "paiza_input_controller.h"

#include <filesystem>
#include <memory>
#include <string>

#include "common/logging.h"
#include "common/paths.h"
#include "common/string_utils.h"
#include "core/candidate_engine.h"
#include "core/cloud_candidate_source.h"
#include "core/input_profile.h"
#include "core/input_session.h"
#include "core/profile_switch.h"
#include "core/key_mapping_store.h"
#include "core/local_dictionary_source.h"
#include "core/localization.h"
#include "core/static_lexicon.h"
#include "core/user_dictionary.h"

#import "candidate_panel.h"
#import "settings_store.h"
#import "settings_window.h"

namespace {

// macOS virtual key codes (kVK_* from Carbon/HIToolbox, hardcoded to
// avoid the Carbon dependency).
constexpr unsigned short kVkReturn = 36;
constexpr unsigned short kVkSpace = 49;
constexpr unsigned short kVkDelete = 51;  // Backspace.
constexpr unsigned short kVkEscape = 53;
constexpr unsigned short kVkPageUp = 116;
constexpr unsigned short kVkPageDown = 121;
constexpr unsigned short kVkLeft = 123;
constexpr unsigned short kVkRight = 124;
constexpr unsigned short kVkDown = 125;
constexpr unsigned short kVkUp = 126;

NSString* WideToNSString(const std::wstring& wide) {
  const std::string utf8 = paiza::WideToUtf8(wide);
  return [NSString stringWithUTF8String:utf8.c_str()] ?: @"";
}

// Process-wide cloud candidate source (registered with the shared engine,
// toggled from the settings window); mirrors the Windows service wiring.
paiza::CloudCandidateSource* g_cloud_source = nullptr;
// Dynamic user lexicon (dual-track overlay, spec §3.2): frequency boosts,
// user-coined words, TXT imports. Persisted to
// ~/Library/Application Support/PaizaIME/user.db asynchronously.
// Static lexicon (compiled lexicon.bin, mmap): prefix + fault-tolerant
// search, bigram context. Ranked below the user overlay, above cloud.
paiza::StaticLexicon* g_static_lexicon = nullptr;

// Input profiles (docs/input-profiles.md). The switch is global because the
// candidate sources are; sessions are per input client, so each one is wired
// through ApplyTo when it is created.
paiza::InputProfileRegistry g_profiles;
std::unique_ptr<paiza::ProfileSwitch> g_profile_switch;
// One user dictionary per profile, in registry order.
std::vector<std::pair<std::string, paiza::UserDictionary*>>
    g_profile_dictionaries;

// Loads the bundled profiles once; a missing directory falls back to the
// built-in Unicode profile.
void LoadProfiles() {
  if (!g_profiles.empty()) return;
  NSString* dir = [[NSBundle mainBundle] resourcePath];
  if (dir != nil) {
    g_profiles.LoadFromDirectory(paiza::Utf8ToWide(
        [[dir stringByAppendingPathComponent:@"profiles"] UTF8String]));
  }
  g_profiles.EnsureDefault();
}

// Builds the profile switch once, binding the shared sources to the Unicode
// profile. Safe to call repeatedly.
paiza::ProfileSwitch* SharedProfileSwitch() {
  if (g_profile_switch == nullptr) {
    LoadProfiles();
    g_profile_switch = std::make_unique<paiza::ProfileSwitch>(
        &g_profiles, /*session=*/nullptr);
    for (const auto& [profile_id, dictionary] : g_profile_dictionaries) {
      paiza::ProfileBinding binding;
      binding.user_dictionary = dictionary;
      // One compiled lexicon today; per-profile lexicons arrive with the
      // second profile.
      if (profile_id == paiza::kDefaultProfileId &&
          g_static_lexicon != nullptr) {
        binding.static_lexicon = g_static_lexicon;
        binding.fixed_sequences = &g_static_lexicon->fixed_sequence_table();
      }
      g_profile_switch->Bind(profile_id, binding);
    }
  }
  return g_profile_switch.get();
}

// The engine and key-mapping store are process-wide (the input method app
// serves every client through one process); sessions are per client.
paiza::CandidateEngine* SharedEngine() {
  static paiza::CandidateEngine* engine = [] {
    auto* built = new paiza::CandidateEngine();
    // One user lexicon per profile: entries learned under one text encoding
    // are meaningless under another, and ProfileSwitch keeps all but the
    // active one unavailable (docs/input-profiles.md).
    LoadProfiles();
    for (const paiza::InputProfile& profile : g_profiles.All()) {
      const std::wstring file =
          profile.user_db.empty() ? L"user.db" : profile.user_db;
      auto user = std::make_unique<paiza::UserDictionary>(
          (std::filesystem::path(paiza::UserConfigDirectory()) / file)
              .wstring());
      g_profile_dictionaries.emplace_back(profile.id, user.get());
      built->AddSource(std::move(user));
    }
    // Compiled static lexicon, if the bundle carries one; otherwise the
    // plain-text dictionary below still serves queries.
    auto lexicon = std::make_unique<paiza::StaticLexicon>();
    NSString* bin = [[NSBundle mainBundle] pathForResource:@"lexicon"
                                                    ofType:@"bin"];
    if (bin != nil && lexicon->Load(paiza::Utf8ToWide([bin UTF8String]))) {
      const std::filesystem::path cfg(paiza::UserConfigDirectory());
      lexicon->LoadTuning((cfg / L"scoring.txt").wstring(),
                          (cfg / L"equivalences.txt").wstring());
      g_static_lexicon = lexicon.get();
      built->AddSource(std::move(lexicon));
    }
    auto dictionary = std::make_unique<paiza::LocalDictionarySource>();
    NSString* path = [[NSBundle mainBundle] pathForResource:@"dictionary"
                                                     ofType:@"txt"];
    if (path != nil) {
      dictionary->LoadFromFile(paiza::Utf8ToWide([path UTF8String]));
    }
    built->AddSource(std::move(dictionary));
    // Cloud stub, disabled until enabled in settings (same as Windows).
    auto cloud = std::make_unique<paiza::CloudCandidateSource>(L"");
    g_cloud_source = cloud.get();
    built->AddSource(std::move(cloud));
    return built;
  }();
  return engine;
}

paiza::KeyMappingStore* SharedMappings() {
  // Hot-reloads ~/Library/Application Support/PaizaIME/keymap.txt (see
  // common/paths.cc) — same file format as Windows.
  static paiza::KeyMappingStore* store = new paiza::KeyMappingStore();
  return store;
}

// Always reads NSUserDefaults (cheap: cfprefsd caches the domain), so a
// Save from the in-process settings window AND from a standalone settings
// instance (double-clicking the app while the IME runs) both take effect
// on the next keystroke without extra plumbing.
paiza::mac::Settings CurrentSettings() {
  return paiza::mac::SettingsStore::Load();
}

void ApplySettings(const paiza::mac::Settings& settings) {
  SharedEngine();  // Ensure g_cloud_source / g_static_lexicon are created.
  // Gate the candidate sources to the selected profile. An unknown id leaves
  // the previous selection in place rather than half-switching.
  SharedProfileSwitch()->Activate(settings.active_profile);
  if (g_cloud_source != nullptr) {
    g_cloud_source->set_enabled(settings.cloud_source_enabled);
  }
  // Rebuild the active soft-equivalence set from the per-pair toggles and
  // load it into the static lexicon — identical to Windows ApplySettings.
  if (g_static_lexicon != nullptr) {
    std::string equiv;
    if (settings.equiv_ou) equiv += "o u\n";
    if (settings.equiv_qv) equiv += "q v\n";
    if (settings.equiv_hg) equiv += "h g\n";
    if (settings.equiv_td) equiv += "t d\n";
    if (settings.equiv_ew) equiv += "E w\n";
    g_static_lexicon->equivalences().LoadFromString(equiv);
  }
}

// One settings window for the whole process.
PaizaSettingsWindowController* SharedSettingsWindow() {
  static PaizaSettingsWindowController* controller = [] {
    SharedEngine();  // Ensure the per-profile dictionaries exist.
    return [[PaizaSettingsWindowController alloc]
        initWithOnChanged:^(const paiza::mac::Settings& settings) {
          ApplySettings(settings);
        }
           userDictionary:SharedProfileSwitch()->active_user_dictionary()];
  }();
  return controller;
}

}  // namespace

@implementation PaizaInputController {
  std::unique_ptr<paiza::InputSession> _session;
  PaizaCandidatePanel* _panel;
}

- (instancetype)initWithServer:(IMKServer*)server
                      delegate:(id)delegate
                        client:(id)inputClient {
  self = [super initWithServer:server delegate:delegate client:inputClient];
  if (self != nil) {
    ApplySettings(CurrentSettings());  // Applies the cloud toggle once.
    _session = std::make_unique<paiza::InputSession>(SharedEngine(),
                                                     SharedMappings());
    // Commits feed the dynamic user lexicon (frequency + coined words) and
    // set the static lexicon's bigram context for the next word.
    // The active profile decides which dictionary and lexicon this session
    // records into, and whether the GB/T fixed sequences apply.
    SharedProfileSwitch()->ApplyTo(*_session);
    _panel = [[PaizaCandidatePanel alloc] init];

    // Mouse interaction on the panel drives the same session the keyboard
    // does; the state is mirrored back to the client afterwards.
    __weak PaizaInputController* weak_self = self;
    _panel.onCandidateChosen = ^(size_t index) {
      PaizaInputController* strong_self = weak_self;
      if (strong_self == nil) return;
      [strong_self applyResult:strong_self->_session->CommitCandidate(index)
                        client:strong_self.client];
    };
    _panel.onPageBackward = ^{
      PaizaInputController* strong_self = weak_self;
      if (strong_self == nil || !strong_self->_session->composing()) return;
      strong_self->_session->PageBackward();
      [strong_self syncStateWithClient:strong_self.client];
    };
    _panel.onPageForward = ^{
      PaizaInputController* strong_self = weak_self;
      if (strong_self == nil || !strong_self->_session->composing()) return;
      strong_self->_session->PageForward();
      [strong_self syncStateWithClient:strong_self.client];
    };
  }
  return self;
}

// The input-source menu (shown from the menu-bar input icon). A single
// "Paiza Settings…" item opens the settings window — the macOS equivalent
// of the Windows settings dialog / wake-up hotkey.
- (NSMenu*)menu {
  PAIZA_LOG_INFO(L"menu requested");
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Paiza"];
  const paiza::UiStrings& strings =
      paiza::GetStrings(CurrentSettings().ui_language);
  const std::string title = paiza::WideToUtf8(std::wstring(
      strings.menu_settings ? strings.menu_settings : L"Settings…"));
  NSMenuItem* item = [[NSMenuItem alloc]
      initWithTitle:[NSString stringWithUTF8String:title.c_str()] ?: @"Settings…"
             action:@selector(showSettings:)
      keyEquivalent:@""];
  item.target = self;
  [menu addItem:item];
  return menu;
}

// Opens the shared settings window. Reached from the input menu item above
// and from IMK's "Open Keyboard Settings…" (showPreferences override).
- (void)showSettings:(id)sender {
  (void)sender;
  PAIZA_LOG_INFO(L"showSettings");
  [SharedSettingsWindow() showSettings];
}

- (void)showPreferences:(id)sender {
  [self showSettings:sender];
}

// Pushes the current settings into this client's session and panel. Cheap
// and idempotent, so it is called on every sync — a Save in the settings
// window then takes effect on the next keystroke without extra plumbing.
- (void)applyCurrentSettings {
  const paiza::mac::Settings settings = CurrentSettings();
  ApplySettings(settings);  // Cloud toggle picks up external saves too.
  _session->set_page_size(settings.candidates_per_page);
  [_panel setCandidateFontFamily:
              [NSString stringWithUTF8String:
                            paiza::WideToUtf8(settings.candidate_font).c_str()]
                  ?: @"Noto Sans Mongolian"];
}

- (NSUInteger)recognizedEvents:(id)sender {
  (void)sender;
  return NSEventMaskKeyDown;
}

// Mirrors the session state into the client: marked text + panel.
- (void)syncStateWithClient:(id)sender {
  [self applyCurrentSettings];
  if (_session->composing()) {
    // How much goes inline is a setting: the host renders it in its own
    // font, which a private-use profile's glyphs are not in. A placeholder
    // keeps the marked range non-empty so the caret rect stays measurable.
    std::wstring inlineText = _session->preedit();
    switch (CurrentSettings().inline_composition) {
      case paiza::InlineComposition::kHidden:
        inlineText.clear();
        break;
      case paiza::InlineComposition::kPlaceholder:
        inlineText = L" ";
        break;
      case paiza::InlineComposition::kFull:
        break;
    }
    NSString* preedit = WideToNSString(inlineText);
    NSDictionary* attributes = @{
      NSUnderlineStyleAttributeName : @(NSUnderlineStyleSingle),
    };
    NSAttributedString* marked =
        [[NSAttributedString alloc] initWithString:preedit
                                        attributes:attributes];
    [sender setMarkedText:marked
           selectionRange:NSMakeRange(preedit.length, 0)
         replacementRange:NSMakeRange(NSNotFound, 0)];

    NSRect caret = NSZeroRect;
    @try {
      [sender attributesForCharacterIndex:0 lineHeightRectangle:&caret];
    } @catch (NSException* exception) {
      caret = NSZeroRect;  // Some hosts refuse; the panel centres itself.
    }
    paiza::Logf(paiza::LogLevel::kInfo,
                L"sync: %zu candidates, page %zu/%zu (size %zu), caret "
                L"(%.0f,%.0f %.0fx%.0f)",
                _session->candidates().size(), _session->current_page() + 1,
                _session->page_count(), _session->page_size(),
                caret.origin.x, caret.origin.y, caret.size.width,
                caret.size.height);
    [_panel updateFromSession:_session.get()];
    [_panel showNearRect:caret];
  } else {
    [sender setMarkedText:@""
           selectionRange:NSMakeRange(0, 0)
         replacementRange:NSMakeRange(NSNotFound, 0)];
    [_panel hidePanel];
  }
}

- (BOOL)applyResult:(paiza::InputSession::KeyResult)result
             client:(id)sender {
  if (result.commit.has_value()) {
    [sender insertText:WideToNSString(*result.commit)
        replacementRange:NSMakeRange(NSNotFound, 0)];
  }
  [self syncStateWithClient:sender];
  return result.handled ? YES : NO;
}

- (BOOL)handleEvent:(NSEvent*)event client:(id)sender {
  if (event.type != NSEventTypeKeyDown) return NO;

  // Command/Control/Option chords belong to the application.
  const NSEventModifierFlags flags =
      event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
  if ((flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl |
                NSEventModifierFlagOption)) != 0) {
    return NO;
  }

  switch (event.keyCode) {
    case kVkReturn:
      return [self applyResult:_session->HandleEnter() client:sender];
    case kVkSpace:
      return [self applyResult:_session->HandleSpace() client:sender];
    case kVkDelete:
      return [self applyResult:_session->HandleBackspace() client:sender];
    case kVkEscape:
      return [self applyResult:_session->HandleEscape() client:sender];
    case kVkPageDown:
      if (!_session->composing()) return NO;
      _session->PageForward();
      [self syncStateWithClient:sender];
      return YES;
    case kVkPageUp:
      if (!_session->composing()) return NO;
      _session->PageBackward();
      [self syncStateWithClient:sender];
      return YES;
    case kVkRight:
    case kVkDown:
      if (!_session->composing()) return NO;
      _session->MoveSelection(+1);
      [self syncStateWithClient:sender];
      return YES;
    case kVkLeft:
    case kVkUp:
      if (!_session->composing()) return NO;
      _session->MoveSelection(-1);
      [self syncStateWithClient:sender];
      return YES;
    default:
      break;
  }

  NSString* characters = event.characters;
  if (characters.length == 1) {
    const unichar c = [characters characterAtIndex:0];
    // event.characters already resolves Shift/Caps, giving the session
    // the case-resolved letter (second letter set) or the mapped
    // punctuation (- _ @ # $ % * by default; keymap.txt-driven).
    if (c < 128 &&
        _session->IsComposableChar(static_cast<char>(c))) {
      return [self applyResult:_session->HandleLetter(static_cast<char>(c))
                        client:sender];
    }
    if (c >= '1' && c <= '9') {
      return [self applyResult:_session->HandleDigit(c - '0')
                        client:sender];
    }
  }

  // Anything else: commit the raw conversion, let the key through.
  return [self applyResult:_session->HandleOtherKey() client:sender];
}

- (void)commitComposition:(id)sender {
  // The host asks us to wrap up (focus change etc.).
  if (_session->composing()) {
    [self applyResult:_session->HandleEnter() client:sender];
  }
}

- (void)deactivateServer:(id)sender {
  (void)sender;
  _session->Reset();
  [_panel hidePanel];
}

@end
