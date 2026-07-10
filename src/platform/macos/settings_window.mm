// Copyright 2026 Paiza. All rights reserved.

#import "settings_window.h"

#import <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "common/logging.h"
#include "common/string_utils.h"
#include "core/font_stem_config.h"
#include "core/key_mapping_store.h"
#include "core/localization.h"
#include "core/mapping_preview.h"

namespace {

// Client-area layout constants (points), mirroring the Windows dialog.
constexpr CGFloat kClientWidth = 520;
constexpr CGFloat kClientHeight = 600;
constexpr CGFloat kMargin = 20;
constexpr CGFloat kCardPadding = 16;
constexpr CGFloat kRowHeight = 30;

NSString* Ns(const wchar_t* w) {
  const std::string s = paiza::WideToUtf8(std::wstring(w ? w : L""));
  return [NSString stringWithUTF8String:s.c_str()] ?: @"";
}

NSString* NsW(const std::wstring& w) {
  const std::string s = paiza::WideToUtf8(w);
  return [NSString stringWithUTF8String:s.c_str()] ?: @"";
}

NSColor* FromRGB(uint32_t rgb) {
  return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xFF) / 255.0
                             green:((rgb >> 8) & 0xFF) / 255.0
                              blue:(rgb & 0xFF) / 255.0
                             alpha:1.0];
}

// True when `family`'s representative font can render U+1820 (MONGOLIAN
// LETTER A) — the same "has a Mongolian glyph" filter the Windows font
// picker uses to float Mongolian-capable fonts to the top.
bool FontSupportsMongolian(NSString* family) {
  NSFont* font = [NSFont fontWithName:family size:16];
  if (font == nil) return false;
  const UniChar ch = 0x1820;
  CGGlyph glyph = 0;
  return CTFontGetGlyphsForCharacters((__bridge CTFontRef)font, &ch, &glyph,
                                      1) &&
         glyph != 0;
}

}  // namespace

// Vertical Mongolian sample: shapes the whole word horizontally with
// CoreText (so the letters connect properly) and rotates it 90° clockwise
// into a top-to-bottom column, stem-centred via fontstems.txt — the same
// technique as the candidate panel. A plain NSTextField cannot do this:
// NSVerticalGlyphFormAttributeName only works in vertical NSTextViews and
// otherwise leaves the glyphs horizontal and disconnected.
@interface PaizaVerticalSampleView : NSView
@property(nonatomic, copy) NSString* text;
@property(nonatomic, copy) NSString* fontFamily;
@property(nonatomic) CGFloat fontSize;
@end

@implementation PaizaVerticalSampleView

- (BOOL)isFlipped {
  return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  if (_text.length == 0) return;
  NSFont* font = [NSFont fontWithName:_fontFamily size:_fontSize]
                     ?: [NSFont systemFontOfSize:_fontSize];
  NSColor* color = [NSColor secondaryLabelColor];
  NSAttributedString* shaped = [[NSAttributedString alloc]
      initWithString:_text
          attributes:@{
            (__bridge id)kCTFontAttributeName : font,
            (__bridge id)kCTForegroundColorAttributeName :
                (__bridge id)color.CGColor,
          }];
  CTLineRef line = CTLineCreateWithAttributedString(
      (__bridge CFAttributedStringRef)shaped);
  CGFloat ascent = 0;
  CGFloat descent = 0;
  CGFloat leading = 0;
  const double length =
      CTLineGetTypographicBounds(line, &ascent, &descent, &leading);

  // Stem-centred on the view's vertical axis; the word run is centred
  // down the view (top-aligned when longer than the view).
  const CGFloat cx = NSMidX(self.bounds);
  const std::optional<float> ratio = paiza::GetFontStemRatio(
      paiza::Utf8ToWide([font.familyName ?: _fontFamily UTF8String] ?: ""));
  const CGFloat base_x =
      ratio.has_value() ? cx - _fontSize * *ratio + descent
                        : cx + (descent - ascent) / 2;
  const CGFloat y_top = std::max(
      0.0, (self.bounds.size.height - static_cast<CGFloat>(length)) / 2);

  CGContextRef context = NSGraphicsContext.currentContext.CGContext;
  CGContextSaveGState(context);
  // Text space -> flipped view: flow runs down, ascent side faces right
  // (90° clockwise, orientation-preserving; see candidate_panel.mm).
  CGContextConcatCTM(context,
                     CGAffineTransformMake(0, 1, 1, 0, base_x, y_top));
  CGContextSetTextMatrix(context, CGAffineTransformIdentity);
  CGContextSetTextPosition(context, 0, 0);
  CTLineDraw(line, context);
  CGContextRestoreGState(context);
  CFRelease(line);
}

@end

// Flipped card-drawing background: window fill + two rounded section cards,
// appearance-aware so the dialog looks native in light and dark mode.
@interface PaizaSettingsBackgroundView : NSView
@property(nonatomic) NSRect generalCard;
@property(nonatomic) NSRect mappingCard;
@end

@implementation PaizaSettingsBackgroundView

- (BOOL)isFlipped {
  return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  const BOOL dark = [[self.effectiveAppearance
      bestMatchFromAppearancesWithNames:@[
        NSAppearanceNameAqua, NSAppearanceNameDarkAqua
      ]] isEqualToString:NSAppearanceNameDarkAqua];
  NSColor* window_bg = dark ? FromRGB(0x1E2024) : FromRGB(0xF3F4F6);
  NSColor* card_bg = dark ? FromRGB(0x2A2D33) : FromRGB(0xFFFFFF);
  NSColor* card_border = dark ? FromRGB(0x3B3F46) : FromRGB(0xE3E5E8);

  [window_bg setFill];
  NSRectFill(self.bounds);
  for (NSValue* value :
       @[ [NSValue valueWithRect:_generalCard],
          [NSValue valueWithRect:_mappingCard] ]) {
    NSBezierPath* card =
        [NSBezierPath bezierPathWithRoundedRect:value.rectValue
                                        xRadius:10
                                        yRadius:10];
    [card_bg setFill];
    [card fill];
    [card_border setStroke];
    [card stroke];
  }
}

@end

@interface PaizaSettingsWindowController () <NSWindowDelegate>
@end

@implementation PaizaSettingsWindowController {
  void (^_onChanged)(const paiza::mac::Settings&);
  paiza::mac::Settings _settings;
  paiza::KeyMappingStore _mappingStore;
  std::vector<std::wstring> _fontFamilies;  // Popup index -> family.

  NSWindow* _window;
  PaizaSettingsBackgroundView* _background;

  NSTextField* _titleLabel;
  NSTextField* _subtitleLabel;
  NSTextField* _generalSection;
  NSTextField* _pageSizeLabel;
  NSTextField* _pageSizeValue;
  NSStepper* _pageSizeStepper;
  NSTextField* _cloudLabel;
  NSSwitch* _cloudSwitch;
  NSTextField* _languageLabel;
  NSPopUpButton* _languagePopup;
  NSTextField* _fontLabel;
  NSPopUpButton* _fontPopup;
  NSTextField* _mappingSection;
  NSButton* _editMappingButton;
  NSTextField* _mappingPreview;
  NSTextField* _mappingHint;
  PaizaVerticalSampleView* _verticalSample;
  NSButton* _saveButton;
  NSButton* _closeButton;
}

- (instancetype)initWithOnChanged:
    (void (^)(const paiza::mac::Settings&))onChanged {
  self = [super init];
  if (self != nil) {
    _onChanged = [onChanged copy];
  }
  return self;
}

- (void)showSettings {
  _settings = paiza::mac::SettingsStore::Load();
  if (_window == nil) {
    [self buildWindow];
  }
  [self applyStrings];
  [self refreshValues];
  [self refreshMappingPreview];

  // The IME runs as a background agent (LSBackgroundOnly); switch to an
  // accessory policy so this real window can activate and take focus
  // without adding a Dock icon.
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  [NSApp activateIgnoringOtherApps:YES];
  [_window center];
  [_window makeKeyAndOrderFront:nil];
  PAIZA_LOG_INFO(L"settings window shown");

  // Debug aid: PAIZA_SETTINGS_SNAPSHOT=<path.png> writes a self-render of
  // the window content — proves in-bundle rendering without screen
  // recording permissions.
  const char* snapshot = getenv("PAIZA_SETTINGS_SNAPSHOT");
  if (snapshot != nullptr) {
    NSView* view = _window.contentView;
    NSBitmapImageRep* rep =
        [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG
                                    properties:@{}];
    [png writeToFile:[NSString stringWithUTF8String:snapshot]
          atomically:YES];
  }
}

// Layout -------------------------------------------------------------------

- (void)buildWindow {
  const NSRect content = NSMakeRect(0, 0, kClientWidth, kClientHeight);
  _window = [[NSWindow alloc]
      initWithContentRect:content
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                  backing:NSBackingStoreBuffered
                    defer:YES];
  _window.releasedWhenClosed = NO;
  _window.titlebarAppearsTransparent = NO;
  _window.delegate = self;  // Red close button routes to windowWillClose:.

  _background = [[PaizaSettingsBackgroundView alloc] initWithFrame:content];
  _background.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  _window.contentView = _background;

  const CGFloat content_width = kClientWidth - 2 * kMargin;
  CGFloat y = kMargin;

  // ---- Header ----------------------------------------------------------
  _titleLabel = [self addLabel:NSMakeRect(kMargin, y, content_width, 28)
                          size:20
                          bold:YES
                         color:[NSColor labelColor]];
  y += 30;
  _subtitleLabel = [self addLabel:NSMakeRect(kMargin, y, content_width, 18)
                             size:12
                             bold:NO
                            color:[NSColor secondaryLabelColor]];
  y += 18 + 14;

  // ---- Card 1: General -------------------------------------------------
  const CGFloat card1_top = y;
  const CGFloat card1_x = kMargin + kCardPadding;
  const CGFloat controls_right = kMargin + content_width - kCardPadding;
  CGFloat row = card1_top + 14;

  _generalSection = [self addLabel:NSMakeRect(card1_x, row, 300, 16)
                              size:11
                              bold:YES
                             color:[NSColor tertiaryLabelColor]];
  row += 16 + 12;

  // Row: candidates per page  [value] [stepper]
  _pageSizeLabel = [self addLabel:NSMakeRect(card1_x, row, 260, kRowHeight)
                             size:14
                             bold:NO
                            color:[NSColor labelColor]];
  _pageSizeStepper = [[NSStepper alloc]
      initWithFrame:NSMakeRect(controls_right - 20, row + 3, 20, 24)];
  _pageSizeStepper.minValue = 1;
  _pageSizeStepper.maxValue = 9;
  _pageSizeStepper.increment = 1;
  _pageSizeStepper.valueWraps = NO;
  _pageSizeStepper.target = self;
  _pageSizeStepper.action = @selector(pageSizeChanged:);
  [_background addSubview:_pageSizeStepper];
  _pageSizeValue = [self addLabel:NSMakeRect(controls_right - 20 - 44, row,
                                             40, kRowHeight)
                             size:15
                             bold:YES
                            color:[NSColor labelColor]];
  _pageSizeValue.alignment = NSTextAlignmentRight;
  row += kRowHeight + 6;

  // Row: cloud candidates toggle.
  _cloudLabel = [self addLabel:NSMakeRect(card1_x, row, 260, kRowHeight)
                          size:14
                          bold:NO
                         color:[NSColor labelColor]];
  _cloudSwitch = [[NSSwitch alloc]
      initWithFrame:NSMakeRect(controls_right - 40, row + 3, 40, 24)];
  _cloudSwitch.target = self;
  _cloudSwitch.action = @selector(cloudToggled:);
  [_background addSubview:_cloudSwitch];
  row += kRowHeight + 6;

  // Row: UI language selector.
  _languageLabel = [self addLabel:NSMakeRect(card1_x, row, 200, kRowHeight)
                             size:14
                             bold:NO
                            color:[NSColor labelColor]];
  _languagePopup = [[NSPopUpButton alloc]
      initWithFrame:NSMakeRect(controls_right - 180, row + 1, 180, 26)
          pullsDown:NO];
  _languagePopup.target = self;
  _languagePopup.action = @selector(languageChanged:);
  [_background addSubview:_languagePopup];
  row += kRowHeight + 6;

  // Row: candidate window font (Mongolian-capable fonts first).
  _fontLabel = [self addLabel:NSMakeRect(card1_x, row, 200, kRowHeight)
                         size:14
                         bold:NO
                        color:[NSColor labelColor]];
  _fontPopup = [[NSPopUpButton alloc]
      initWithFrame:NSMakeRect(controls_right - 240, row + 1, 240, 26)
          pullsDown:NO];
  _fontPopup.target = self;
  _fontPopup.action = @selector(fontChanged:);
  [_background addSubview:_fontPopup];
  row += kRowHeight + 12;

  _background.generalCard =
      NSMakeRect(kMargin, card1_top, content_width, row - card1_top);
  y = row + 16;

  // ---- Card 2: Key mapping ---------------------------------------------
  const CGFloat card2_top = y;
  const CGFloat card2_x = kMargin + kCardPadding;
  row = card2_top + 14;

  _mappingSection =
      [self addLabel:NSMakeRect(card2_x, row + 4, content_width - 220, 20)
                size:11
                bold:YES
               color:[NSColor tertiaryLabelColor]];
  _editMappingButton = [[NSButton alloc]
      initWithFrame:NSMakeRect(controls_right - 190, row, 190, 28)];
  _editMappingButton.bezelStyle = NSBezelStyleRounded;
  _editMappingButton.target = self;
  _editMappingButton.action = @selector(editMapping:);
  [_background addSubview:_editMappingButton];
  row += 28 + 10;

  _mappingPreview =
      [self addLabel:NSMakeRect(card2_x, row, content_width - 2 * kCardPadding -
                                                  52,
                                180)
                size:14
                bold:NO
               color:[NSColor labelColor]];
  _mappingPreview.font = [NSFont monospacedSystemFontOfSize:13
                                                     weight:NSFontWeightRegular];
  [_mappingPreview.cell setWraps:YES];
  _mappingPreview.maximumNumberOfLines = 0;

  // Vertical Mongolian sample down the card's right edge ("Monggol"),
  // rendered in the chosen candidate font (live preview of the setting).
  _verticalSample = [[PaizaVerticalSampleView alloc]
      initWithFrame:NSMakeRect(kMargin + content_width - kCardPadding - 36,
                               row, 36, 180)];
  _verticalSample.text = @"ᠮᠣᠩᠭᠣᠯ";
  _verticalSample.fontSize = 24;
  [_background addSubview:_verticalSample];
  row += 180 + 8;

  // Two lines: the path alone often fills the first line.
  _mappingHint = [self addLabel:NSMakeRect(card2_x, row,
                                           content_width - 2 * kCardPadding, 30)
                           size:11
                           bold:NO
                          color:[NSColor tertiaryLabelColor]];
  row += 30 + 10;
  _background.mappingCard =
      NSMakeRect(kMargin, card2_top, content_width, row - card2_top);

  // ---- Footer ----------------------------------------------------------
  const CGFloat footer_y = kClientHeight - kMargin - 32;
  _saveButton = [[NSButton alloc]
      initWithFrame:NSMakeRect(kClientWidth - kMargin - 100, footer_y, 100,
                               32)];
  _saveButton.bezelStyle = NSBezelStyleRounded;
  _saveButton.keyEquivalent = @"\r";  // Default button (Return).
  _saveButton.target = self;
  _saveButton.action = @selector(save:);
  [_background addSubview:_saveButton];

  _closeButton = [[NSButton alloc]
      initWithFrame:NSMakeRect(kClientWidth - kMargin - 100 - 10 - 100,
                               footer_y, 100, 32)];
  _closeButton.bezelStyle = NSBezelStyleRounded;
  _closeButton.keyEquivalent = @"\033";  // Escape.
  _closeButton.target = self;
  _closeButton.action = @selector(closeWindow:);
  [_background addSubview:_closeButton];
}

- (NSTextField*)addLabel:(NSRect)frame
                    size:(CGFloat)size
                    bold:(BOOL)bold
                   color:(NSColor*)color {
  NSTextField* label = [[NSTextField alloc] initWithFrame:frame];
  label.editable = NO;
  label.selectable = NO;
  label.bordered = NO;
  label.drawsBackground = NO;
  label.font = bold ? [NSFont boldSystemFontOfSize:size]
                    : [NSFont systemFontOfSize:size];
  label.textColor = color;
  [label.cell setLineBreakMode:NSLineBreakByWordWrapping];
  [_background addSubview:label];
  return label;
}

// Localized strings --------------------------------------------------------

- (void)applyStrings {
  const paiza::UiStrings& s = paiza::GetStrings(_settings.ui_language);
  _window.title = Ns(s.window_title);
  _titleLabel.stringValue = Ns(s.app_title);
  _subtitleLabel.stringValue = Ns(s.subtitle);
  _generalSection.stringValue = Ns(s.section_general);
  _pageSizeLabel.stringValue = Ns(s.candidates_per_page);
  _cloudLabel.stringValue = Ns(s.cloud_candidates);
  _languageLabel.stringValue = Ns(s.language_label);
  _fontLabel.stringValue = Ns(s.candidate_font_label);
  _mappingSection.stringValue = Ns(s.section_key_mapping);
  _editMappingButton.title = Ns(s.edit_mapping_file);
  _saveButton.title = Ns(s.save);
  _closeButton.title = Ns(s.close);

  // Language popup: the three UI languages by their own display names.
  [_languagePopup removeAllItems];
  for (uint32_t i = 0; i < paiza::kUiLanguageCount; ++i) {
    [_languagePopup addItemWithTitle:
                        Ns(paiza::GetStrings(paiza::UiLanguageFromValue(i))
                               .language_name)];
  }
  [_languagePopup selectItemAtIndex:
                      static_cast<NSInteger>(_settings.ui_language)];

  [self rebuildFontPopup];

  // Localized hint with the real keymap.txt path substituted for "%ls"
  // (manual replace — locale-independent, unlike wide swprintf which can
  // choke on the em dash in the format string).
  std::wstring hint = s.mapping_hint_generic;
  const size_t placeholder = hint.find(L"%ls");
  if (placeholder != std::wstring::npos) {
    hint.replace(placeholder, 3, paiza::KeyMappingFilePath());
  }
  _mappingHint.stringValue = NsW(hint);

  // Force the preview to rebuild with the localized row labels.
  [self forceRefreshMappingPreview];
}

- (void)rebuildFontPopup {
  _fontFamilies.clear();
  [_fontPopup removeAllItems];

  NSArray<NSString*>* families =
      [[NSFontManager sharedFontManager] availableFontFamilies];
  NSMutableArray<NSString*>* mongolian = [NSMutableArray array];
  NSMutableArray<NSString*>* others = [NSMutableArray array];
  for (NSString* family in families) {
    if (FontSupportsMongolian(family)) {
      [mongolian addObject:family];
    } else {
      [others addObject:family];
    }
  }
  NSArray<NSString*>* sorted_mn = [mongolian
      sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
  NSArray<NSString*>* sorted_others = [others
      sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];

  NSInteger selected = 0;
  const NSString* current = NsW(_settings.candidate_font);
  // Mongolian-capable fonts first (marked with U+1820), then the rest.
  for (NSString* family in sorted_mn) {
    [_fontPopup addItemWithTitle:[family stringByAppendingString:@"  ᠠ"]];
    _fontPopup.lastItem.representedObject = family;
    if ([family isEqualToString:(NSString*)current]) {
      selected = _fontPopup.numberOfItems - 1;
    }
    _fontFamilies.push_back(paiza::Utf8ToWide([family UTF8String]));
  }
  if (sorted_mn.count > 0 && sorted_others.count > 0) {
    [_fontPopup.menu addItem:[NSMenuItem separatorItem]];
    _fontFamilies.push_back(L"");  // Placeholder aligning with separator.
  }
  for (NSString* family in sorted_others) {
    [_fontPopup addItemWithTitle:family];
    _fontPopup.lastItem.representedObject = family;
    if ([family isEqualToString:(NSString*)current]) {
      selected = _fontPopup.numberOfItems - 1;
    }
    _fontFamilies.push_back(paiza::Utf8ToWide([family UTF8String]));
  }
  [_fontPopup selectItemAtIndex:selected];
}

- (void)refreshValues {
  _pageSizeValue.stringValue =
      [NSString stringWithFormat:@"%u", _settings.candidates_per_page];
  _pageSizeStepper.integerValue = _settings.candidates_per_page;
  _cloudSwitch.state =
      _settings.cloud_source_enabled ? NSControlStateValueOn
                                     : NSControlStateValueOff;
  // Live preview of the chosen candidate font, drawn as connected
  // vertical Mongolian by PaizaVerticalSampleView.
  _verticalSample.fontFamily = NsW(_settings.candidate_font);
  _verticalSample.needsDisplay = YES;
}

- (void)refreshMappingPreview {
  const paiza::KeyMapping& mapping = _mappingStore.Get();
  _mappingPreview.stringValue = NsW(paiza::BuildMappingPreview(
      mapping, paiza::GetStrings(_settings.ui_language)));
}

- (void)forceRefreshMappingPreview {
  _mappingPreview.stringValue = NsW(paiza::BuildMappingPreview(
      _mappingStore.Get(), paiza::GetStrings(_settings.ui_language)));
}

// Actions ------------------------------------------------------------------

- (void)pageSizeChanged:(id)sender {
  (void)sender;
  _settings.candidates_per_page =
      static_cast<uint32_t>(_pageSizeStepper.integerValue);
  [self refreshValues];
}

- (void)cloudToggled:(id)sender {
  (void)sender;
  _settings.cloud_source_enabled =
      _cloudSwitch.state == NSControlStateValueOn;
}

- (void)languageChanged:(id)sender {
  (void)sender;
  _settings.ui_language =
      paiza::UiLanguageFromValue(
          static_cast<uint32_t>(_languagePopup.indexOfSelectedItem));
  [self applyStrings];
  [self refreshValues];
}

- (void)fontChanged:(id)sender {
  (void)sender;
  NSString* family = _fontPopup.selectedItem.representedObject;
  if (family.length == 0) return;  // Separator row.
  _settings.candidate_font = paiza::Utf8ToWide([family UTF8String]);
  [self refreshValues];
}

- (void)editMapping:(id)sender {
  (void)sender;
  const std::wstring path = paiza::KeyMappingFilePath();
  if (path.empty()) return;
  NSString* ns_path = NsW(path);
  // First use: materialise the default mapping so the user edits a
  // complete, commented file rather than starting from scratch.
  if (![[NSFileManager defaultManager] fileExistsAtPath:ns_path]) {
    _mappingStore.Get().SaveToFile(path);
  }
  [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:ns_path]];
}

- (void)save:(id)sender {
  (void)sender;
  paiza::mac::SettingsStore::Save(_settings);
  if (_onChanged != nil) _onChanged(_settings);
  [self closeWindow:nil];
}

- (void)closeWindow:(id)sender {
  (void)sender;
  [_window orderOut:nil];
  // Return to the background-agent policy so no stray Dock icon lingers.
  [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
  if (_onClosed != nil) _onClosed();
}

// The title bar's red close button: same cleanup as the Close button.
- (void)windowWillClose:(NSNotification*)notification {
  (void)notification;
  [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
  if (_onClosed != nil) _onClosed();
}

@end
