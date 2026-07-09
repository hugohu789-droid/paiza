// Copyright 2026 Paiza. All rights reserved.

#import "paiza_input_controller.h"

#include <memory>
#include <string>

#include "common/string_utils.h"
#include "core/candidate_engine.h"
#include "core/input_session.h"
#include "core/key_mapping_store.h"
#include "core/local_dictionary_source.h"

#import "candidate_panel.h"

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

// The engine and key-mapping store are process-wide (the input method app
// serves every client through one process); sessions are per client.
paiza::CandidateEngine* SharedEngine() {
  static paiza::CandidateEngine* engine = [] {
    auto* built = new paiza::CandidateEngine();
    auto dictionary = std::make_unique<paiza::LocalDictionarySource>();
    NSString* path = [[NSBundle mainBundle] pathForResource:@"dictionary"
                                                     ofType:@"txt"];
    if (path != nil) {
      dictionary->LoadFromFile(paiza::Utf8ToWide([path UTF8String]));
    }
    built->AddSource(std::move(dictionary));
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
    _session = std::make_unique<paiza::InputSession>(SharedEngine(),
                                                     SharedMappings());
    _panel = [[PaizaCandidatePanel alloc] init];
  }
  return self;
}

- (NSUInteger)recognizedEvents:(id)sender {
  (void)sender;
  return NSEventMaskKeyDown;
}

// Mirrors the session state into the client: marked text + panel.
- (void)syncStateWithClient:(id)sender {
  if (_session->composing()) {
    NSString* preedit = WideToNSString(_session->preedit());
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
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
      // event.characters already resolves Shift/Caps to the letter case,
      // which is exactly the "second letter set" input the session wants.
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
