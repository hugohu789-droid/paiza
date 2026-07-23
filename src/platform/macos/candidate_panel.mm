// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#import "candidate_panel.h"

#import <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "common/string_utils.h"
#include "core/font_stem_config.h"
#include "core/input_session.h"

namespace {

// Layout constants mirror the Windows CandidateWindow/CandidateListView so
// both platforms read as the same product (logical points).
constexpr CGFloat kPadding = 6.0;
constexpr CGFloat kNumberBandHeight = 15.0;
constexpr CGFloat kMinColumnWidth = 22.0;
constexpr CGFloat kMaxTextBandHeight = 340.0;
// Snug horizontal padding around each vertical glyph so columns hug the
// text instead of floating in whitespace.
constexpr CGFloat kColumnSidePadding = 3.0;
constexpr CGFloat kTextBandPadding = 4.0;
constexpr CGFloat kFooterHeight = 24.0;
constexpr CGFloat kPageButtonWidth = 22.0;
constexpr CGFloat kPageButtonHeight = 18.0;
constexpr CGFloat kPageLabelWidth = 40.0;
constexpr CGFloat kCornerRadius = 8.0;
constexpr CGFloat kCandidateFontSize = 16.0;
constexpr CGFloat kNumberFontSize = 9.5;

// The Windows premium palette, with a matching dark-mode variant so the
// panel looks native under both macOS appearances.
struct Palette {
  NSColor* window_bg;
  NSColor* border;
  NSColor* number;
  NSColor* number_selected;
  NSColor* text;
  NSColor* text_selected;
  NSColor* selected_bg;
  NSColor* hover_bg;
  NSColor* footer_bg;
  NSColor* separator;
  NSColor* page_label;
  NSColor* button_border;
  NSColor* button_glyph;
  NSColor* button_glyph_disabled;
  NSColor* button_hover_bg;
};

NSColor* FromRGB(uint32_t rgb) {
  return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xFF) / 255.0
                             green:((rgb >> 8) & 0xFF) / 255.0
                              blue:(rgb & 0xFF) / 255.0
                             alpha:1.0];
}

const Palette& LightPalette() {
  static const Palette palette = {
      .window_bg = FromRGB(0xFFFFFF),
      .border = FromRGB(0xE2E4E8),
      .number = FromRGB(0xAAB0B8),
      .number_selected = FromRGB(0x2563EB),
      .text = FromRGB(0x1F2430),
      .text_selected = FromRGB(0x16305C),
      .selected_bg = FromRGB(0xEAF1FE),
      .hover_bg = FromRGB(0xF3F5F8),
      .footer_bg = FromRGB(0xFBFBFC),
      .separator = FromRGB(0xEDEEF1),
      .page_label = FromRGB(0xAAB0B8),
      .button_border = FromRGB(0xE2E4E8),
      .button_glyph = FromRGB(0x5F6368),
      .button_glyph_disabled = FromRGB(0xC6CAD0),
      .button_hover_bg = FromRGB(0xF3F5F8),
  };
  return palette;
}

const Palette& DarkPalette() {
  static const Palette palette = {
      .window_bg = FromRGB(0x24272D),
      .border = FromRGB(0x3B3F46),
      .number = FromRGB(0x80868F),
      .number_selected = FromRGB(0x7AA5FF),
      .text = FromRGB(0xE8EAED),
      .text_selected = FromRGB(0xFFFFFF),
      .selected_bg = FromRGB(0x2C3D5C),
      .hover_bg = FromRGB(0x2D3138),
      .footer_bg = FromRGB(0x212429),
      .separator = FromRGB(0x33373E),
      .page_label = FromRGB(0x80868F),
      .button_border = FromRGB(0x3B3F46),
      .button_glyph = FromRGB(0xB6BBC2),
      .button_glyph_disabled = FromRGB(0x565B63),
      .button_hover_bg = FromRGB(0x2D3138),
  };
  return palette;
}

const Palette& PaletteFor(NSAppearance* appearance) {
  NSAppearanceName name = [appearance
      bestMatchFromAppearancesWithNames:@[
        NSAppearanceNameAqua, NSAppearanceNameDarkAqua
      ]];
  return [name isEqualToString:NSAppearanceNameDarkAqua] ? DarkPalette()
                                                         : LightPalette();
}

// One candidate column of the current page.
struct PanelColumn {
  NSString* number;
  NSString* text;
  size_t global_index;
  bool selected;
  CGFloat width;        // Hugging width incl. side padding.
  CGFloat word_length;  // Rotated glyph run length (down the column).
  CGFloat ascent;       // Horizontal-layout metrics of the shaped word.
  CGFloat descent;
};

struct WordMetrics {
  CGFloat length;
  CGFloat ascent;
  CGFloat descent;
};

WordMetrics MeasureWord(NSString* text, NSFont* font) {
  NSAttributedString* shaped = [[NSAttributedString alloc]
      initWithString:text
          attributes:@{(__bridge id)kCTFontAttributeName : font}];
  CTLineRef line = CTLineCreateWithAttributedString(
      (__bridge CFAttributedStringRef)shaped);
  CGFloat ascent = 0;
  CGFloat descent = 0;
  CGFloat leading = 0;
  const double length =
      CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
  CFRelease(line);
  return {static_cast<CGFloat>(length), ascent, descent};
}

}  // namespace

// Draws the candidate columns, footer and paging buttons; owns the mouse
// interaction (hover + click) inside the panel.
@interface PaizaCandidateContentView : NSView
@property(nonatomic) std::vector<PanelColumn> columns;
@property(nonatomic, copy) NSString* pageLabel;
@property(nonatomic) BOOL footerVisible;
@property(nonatomic) BOOL canPageBackward;
@property(nonatomic) BOOL canPageForward;
@property(nonatomic) CGFloat textBandHeight;
@property(nonatomic, strong) NSFont* candidateFont;
@property(nonatomic, copy) NSString* stemFamily;  // For fontstems.txt lookup.
@property(nonatomic) BOOL debugSpine;
@property(nonatomic, copy) void (^onChoose)(size_t index);
@property(nonatomic, copy) void (^onPrev)(void);
@property(nonatomic, copy) void (^onNext)(void);
@end

@implementation PaizaCandidateContentView {
  size_t _hoveredIndex;   // Global candidate index, SIZE_MAX when none.
  int _hoveredButton;     // 0 none, 1 prev, 2 next.
  NSTrackingArea* _trackingArea;
}

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self != nil) {
    _hoveredIndex = SIZE_MAX;
    _hoveredButton = 0;
    _textBandHeight = 150;
  }
  return self;
}

- (BOOL)isFlipped {
  return YES;  // Columns and bands lay out top-down.
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  (void)event;
  return YES;  // The panel never becomes key; clicks must land anyway.
}

// Geometry -----------------------------------------------------------------

- (CGFloat)columnsTotalWidth {
  CGFloat total = 0;
  for (const PanelColumn& column : _columns) total += column.width;
  return total;
}

- (CGFloat)columnsOriginX {
  // Centre the candidate strip when the footer forces extra width.
  return std::max(kPadding,
                  (self.bounds.size.width - [self columnsTotalWidth]) / 2);
}

- (NSRect)columnRect:(size_t)position {
  CGFloat x = [self columnsOriginX];
  for (size_t i = 0; i < position && i < _columns.size(); ++i) {
    x += _columns[i].width;
  }
  return NSMakeRect(x, kPadding, _columns[position].width,
                    kNumberBandHeight + _textBandHeight);
}

- (NSRect)prevButtonRect {
  const CGFloat footer_y = self.bounds.size.height - kFooterHeight;
  const CGFloat button_y =
      footer_y + (kFooterHeight - kPageButtonHeight) / 2;
  return NSMakeRect(
      self.bounds.size.width - kPadding - 2 * kPageButtonWidth - 4,
      button_y, kPageButtonWidth, kPageButtonHeight);
}

- (NSRect)nextButtonRect {
  const CGFloat footer_y = self.bounds.size.height - kFooterHeight;
  const CGFloat button_y =
      footer_y + (kFooterHeight - kPageButtonHeight) / 2;
  return NSMakeRect(self.bounds.size.width - kPadding - kPageButtonWidth,
                    button_y, kPageButtonWidth, kPageButtonHeight);
}

- (NSRect)pageLabelRect {
  const NSRect prev = [self prevButtonRect];
  return NSMakeRect(prev.origin.x - 4 - kPageLabelWidth, prev.origin.y,
                    kPageLabelWidth, kPageButtonHeight);
}

// Drawing ------------------------------------------------------------------

// Draws `text` rotated 90° clockwise so it reads top-to-bottom, centred on
// the Mongolian stem when the font has a fontstems.txt calibration entry
// (otherwise centred on the glyph box). `rect` is the column's text band in
// flipped view coordinates.
- (void)drawVerticalWord:(NSString*)text
                  column:(const PanelColumn&)column
                  inRect:(NSRect)rect
                   color:(NSColor*)color {
  CGContextRef context = NSGraphicsContext.currentContext.CGContext;
  NSAttributedString* shaped = [[NSAttributedString alloc]
      initWithString:text
          attributes:@{
            (__bridge id)kCTFontAttributeName : _candidateFont,
            (__bridge id)kCTForegroundColorAttributeName :
                (__bridge id)color.CGColor,
          }];
  CTLineRef line = CTLineCreateWithAttributedString(
      (__bridge CFAttributedStringRef)shaped);

  const CGFloat cx = NSMidX(rect);
  // After the rotation the column box spans [base - descent, base + ascent]
  // where `base` is the x position of the horizontal baseline. Stem-centred
  // fonts put the stem `font_size * ratio` right of the box's leading
  // (left) edge; without calibration the box itself is centred.
  const std::optional<float> ratio = paiza::GetFontStemRatio(
      paiza::Utf8ToWide([_stemFamily UTF8String] ?: ""));
  CGFloat base_x;
  if (ratio.has_value()) {
    base_x = cx - kCandidateFontSize * *ratio + column.descent;
  } else {
    base_x = cx + (column.descent - column.ascent) / 2;
  }

  CGContextSaveGState(context);
  // Maps shaped text space (x = flow, y = ascent side) into the flipped
  // view: flow runs down the column, the ascent side faces right — the
  // 90°-clockwise rotation vertical Mongolian requires. Composed with the
  // view's flip this is orientation-preserving, so glyphs never mirror.
  CGContextConcatCTM(context, CGAffineTransformMake(0, 1, 1, 0, base_x,
                                                    NSMinY(rect)));
  CGContextSetTextMatrix(context, CGAffineTransformIdentity);
  CGContextSetTextPosition(context, 0, 0);
  CTLineDraw(line, context);
  CGContextRestoreGState(context);
  CFRelease(line);

  if (_debugSpine) {
    // Reference line down the column centre: every character's stem must
    // land on (or a constant distance from) this line.
    [[NSColor colorWithSRGBRed:0.88 green:0.08 blue:0.08 alpha:0.75] set];
    NSRectFill(NSMakeRect(cx, NSMinY(rect), 1, rect.size.height));
  }
}

- (void)drawPageButton:(NSRect)rect
                 glyph:(NSString*)glyph
               enabled:(BOOL)enabled
               hovered:(BOOL)hovered
               palette:(const Palette&)palette {
  NSBezierPath* shape = [NSBezierPath
      bezierPathWithRoundedRect:NSInsetRect(rect, 0.5, 0.5)
                        xRadius:4
                        yRadius:4];
  if (enabled && hovered) {
    [palette.button_hover_bg setFill];
    [shape fill];
  }
  [palette.button_border setStroke];
  [shape stroke];

  NSDictionary* attributes = @{
    NSFontAttributeName : [NSFont systemFontOfSize:12 weight:
                                     NSFontWeightMedium],
    NSForegroundColorAttributeName :
        enabled ? palette.button_glyph : palette.button_glyph_disabled,
  };
  const NSSize size = [glyph sizeWithAttributes:attributes];
  [glyph drawAtPoint:NSMakePoint(NSMidX(rect) - size.width / 2,
                                 NSMidY(rect) - size.height / 2)
      withAttributes:attributes];
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  const Palette& palette = PaletteFor(self.effectiveAppearance);
  const NSRect bounds = self.bounds;

  // Rounded card: fill, footer strip, hairline border.
  NSBezierPath* card = [NSBezierPath
      bezierPathWithRoundedRect:NSInsetRect(bounds, 0.5, 0.5)
                        xRadius:kCornerRadius
                        yRadius:kCornerRadius];
  [palette.window_bg setFill];
  [card fill];

  if (_footerVisible) {
    [NSGraphicsContext saveGraphicsState];
    [card addClip];
    const NSRect footer =
        NSMakeRect(0, bounds.size.height - kFooterHeight, bounds.size.width,
                   kFooterHeight);
    [palette.footer_bg setFill];
    NSRectFill(footer);
    [palette.separator setFill];
    NSRectFill(NSMakeRect(0, footer.origin.y, bounds.size.width, 1));
    [NSGraphicsContext restoreGraphicsState];
  }

  // Columns: pill highlight, number band, vertical word.
  NSFont* number_font = [NSFont monospacedDigitSystemFontOfSize:kNumberFontSize
                                                          weight:
                                                              NSFontWeightRegular];
  NSFont* number_font_bold =
      [NSFont monospacedDigitSystemFontOfSize:kNumberFontSize
                                       weight:NSFontWeightSemibold];
  for (size_t i = 0; i < _columns.size(); ++i) {
    const PanelColumn& column = _columns[i];
    const NSRect column_rect = [self columnRect:i];

    // A slim rounded pill hugs the column; the accent tint reads as a
    // premium highlight rather than a heavy block (same as Windows).
    if (column.selected || column.global_index == _hoveredIndex) {
      [(column.selected ? palette.selected_bg : palette.hover_bg) setFill];
      [[NSBezierPath
          bezierPathWithRoundedRect:NSInsetRect(column_rect, 3, 1)
                            xRadius:7
                            yRadius:7] fill];
    }

    // Number above the candidate text; accent + semibold when selected.
    NSDictionary* number_attributes = @{
      NSFontAttributeName : column.selected ? number_font_bold : number_font,
      NSForegroundColorAttributeName :
          column.selected ? palette.number_selected : palette.number,
    };
    const NSSize number_size =
        [column.number sizeWithAttributes:number_attributes];
    [column.number
        drawAtPoint:NSMakePoint(NSMidX(column_rect) - number_size.width / 2,
                                column_rect.origin.y +
                                    (kNumberBandHeight - number_size.height) /
                                        2)
        withAttributes:number_attributes];

    const NSRect text_rect =
        NSMakeRect(column_rect.origin.x,
                   column_rect.origin.y + kNumberBandHeight,
                   column_rect.size.width, _textBandHeight);
    [self drawVerticalWord:column.text
                    column:column
                    inRect:text_rect
                     color:column.selected ? palette.text_selected
                                           : palette.text];
  }

  // Footer, bottom-right: [1/3] ‹ ›
  if (_footerVisible) {
    NSDictionary* label_attributes = @{
      NSFontAttributeName : [NSFont monospacedDigitSystemFontOfSize:10.5
                                                              weight:
                                                                  NSFontWeightRegular],
      NSForegroundColorAttributeName : palette.page_label,
    };
    const NSRect label_rect = [self pageLabelRect];
    const NSSize label_size =
        [_pageLabel sizeWithAttributes:label_attributes];
    [_pageLabel
        drawAtPoint:NSMakePoint(NSMidX(label_rect) - label_size.width / 2,
                                NSMidY(label_rect) - label_size.height / 2)
        withAttributes:label_attributes];

    [self drawPageButton:[self prevButtonRect]
                   glyph:@"‹"
                 enabled:_canPageBackward
                 hovered:_hoveredButton == 1
                 palette:palette];
    [self drawPageButton:[self nextButtonRect]
                   glyph:@"›"
                 enabled:_canPageForward
                 hovered:_hoveredButton == 2
                 palette:palette];
  }

  [palette.border setStroke];
  [card setLineWidth:1.0];
  [card stroke];
}

// Mouse --------------------------------------------------------------------

- (void)updateTrackingAreas {
  [super updateTrackingAreas];
  if (_trackingArea != nil) [self removeTrackingArea:_trackingArea];
  _trackingArea = [[NSTrackingArea alloc]
      initWithRect:NSZeroRect
           options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
                   NSTrackingActiveAlways | NSTrackingInVisibleRect
             owner:self
          userInfo:nil];
  [self addTrackingArea:_trackingArea];
}

- (BOOL)hitColumn:(NSPoint)point index:(size_t*)index {
  for (size_t i = 0; i < _columns.size(); ++i) {
    if (NSPointInRect(point, [self columnRect:i])) {
      *index = _columns[i].global_index;
      return YES;
    }
  }
  return NO;
}

- (void)mouseMoved:(NSEvent*)event {
  const NSPoint point = [self convertPoint:event.locationInWindow
                                  fromView:nil];
  size_t hovered = SIZE_MAX;
  [self hitColumn:point index:&hovered];
  int button = 0;
  if (_footerVisible) {
    if (NSPointInRect(point, [self prevButtonRect])) button = 1;
    if (NSPointInRect(point, [self nextButtonRect])) button = 2;
  }
  if (hovered != _hoveredIndex || button != _hoveredButton) {
    _hoveredIndex = hovered;
    _hoveredButton = button;
    self.needsDisplay = YES;
  }
  const bool clickable =
      hovered != SIZE_MAX || (button == 1 && _canPageBackward) ||
      (button == 2 && _canPageForward);
  [clickable ? [NSCursor pointingHandCursor] : [NSCursor arrowCursor] set];
}

- (void)mouseExited:(NSEvent*)event {
  (void)event;
  if (_hoveredIndex != SIZE_MAX || _hoveredButton != 0) {
    _hoveredIndex = SIZE_MAX;
    _hoveredButton = 0;
    self.needsDisplay = YES;
  }
  [[NSCursor arrowCursor] set];
}

- (void)mouseDown:(NSEvent*)event {
  const NSPoint point = [self convertPoint:event.locationInWindow
                                  fromView:nil];
  if (_footerVisible) {
    if (NSPointInRect(point, [self prevButtonRect])) {
      if (_canPageBackward && _onPrev) _onPrev();
      return;
    }
    if (NSPointInRect(point, [self nextButtonRect])) {
      if (_canPageForward && _onNext) _onNext();
      return;
    }
  }
  size_t index = SIZE_MAX;
  if ([self hitColumn:point index:&index] && _onChoose) {
    _onChoose(index);
  }
}

@end

@implementation PaizaCandidatePanel {
  PaizaCandidateContentView* _content;
  NSString* _requestedFontFamily;
}

- (instancetype)init {
  self = [super initWithContentRect:NSMakeRect(0, 0, 200, 160)
                          styleMask:NSWindowStyleMaskBorderless |
                                    NSWindowStyleMaskNonactivatingPanel
                            backing:NSBackingStoreBuffered
                              defer:YES];
  if (self != nil) {
    self.level = NSPopUpMenuWindowLevel;
    self.hasShadow = YES;
    self.opaque = NO;
    self.backgroundColor = [NSColor clearColor];
    // Never steal focus from the host application.
    self.becomesKeyOnlyIfNeeded = YES;
    // Follow the host into every Space, including full-screen apps.
    self.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                              NSWindowCollectionBehaviorFullScreenAuxiliary;
    _content = [[PaizaCandidateContentView alloc]
        initWithFrame:NSMakeRect(0, 0, 200, 160)];
    // Debug aid, mirrors Windows SetDebugSpine:
    //   defaults write com.paiza.inputmethod.mongolian PaizaDebugSpine -bool YES
    _content.debugSpine = [[NSUserDefaults standardUserDefaults]
        boolForKey:@"PaizaDebugSpine"];
    self.contentView = _content;
    [self setCandidateFontFamily:@"Noto Sans Mongolian"];

    __weak PaizaCandidatePanel* weak_self = self;
    _content.onChoose = ^(size_t index) {
      PaizaCandidatePanel* strong_self = weak_self;
      if (strong_self != nil && strong_self.onCandidateChosen != nil) {
        strong_self.onCandidateChosen(index);
      }
    };
    _content.onPrev = ^{
      PaizaCandidatePanel* strong_self = weak_self;
      if (strong_self != nil && strong_self.onPageBackward != nil) {
        strong_self.onPageBackward();
      }
    };
    _content.onNext = ^{
      PaizaCandidatePanel* strong_self = weak_self;
      if (strong_self != nil && strong_self.onPageForward != nil) {
        strong_self.onPageForward();
      }
    };
  }
  return self;
}

- (BOOL)canBecomeKeyWindow {
  return NO;
}

- (void)setCandidateFontFamily:(NSString*)family {
  if (family.length == 0 || [family isEqualToString:_requestedFontFamily]) {
    return;
  }
  _requestedFontFamily = [family copy];
  NSFont* font = [NSFont fontWithName:family size:kCandidateFontSize];
  // Missing fonts fall back to the system font; CoreText then substitutes
  // a Mongolian-capable fallback per glyph at draw time.
  _content.candidateFont = font ?: [NSFont systemFontOfSize:kCandidateFontSize];
  _content.stemFamily = _content.candidateFont.familyName ?: family;
}

- (void)updateFromSession:(paiza::InputSession*)session {
  std::vector<PanelColumn> columns;
  size_t first = 0;
  size_t last = 0;
  session->PageRange(&first, &last);
  CGFloat longest_word = 48;
  for (size_t i = first; i < last; ++i) {
    PanelColumn column;
    column.number = [NSString
        stringWithFormat:@"%lu",
                         static_cast<unsigned long>(i - first + 1)];
    const std::string utf8 =
        paiza::WideToUtf8(session->candidates()[i].text);
    column.text = [NSString stringWithUTF8String:utf8.c_str()] ?: @"";
    column.global_index = i;
    column.selected = i == session->selected_index();

    // Measure with the same shaping used for painting so long words are
    // never truncated; each column hugs its own glyph thickness.
    const WordMetrics metrics =
        MeasureWord(column.text, _content.candidateFont);
    column.word_length = metrics.length;
    column.ascent = metrics.ascent;
    column.descent = metrics.descent;
    CGFloat glyph_thickness = metrics.ascent + metrics.descent;
    const std::optional<float> ratio = paiza::GetFontStemRatio(
        paiza::Utf8ToWide([_content.stemFamily UTF8String] ?: ""));
    if (ratio.has_value()) {
      // Stem-centred display shifts the glyph box inside the column, so
      // widen to twice the larger stem-side extent to avoid clipping.
      const CGFloat stem = kCandidateFontSize * *ratio;
      glyph_thickness = 2 * std::max(stem, glyph_thickness - stem);
    }
    column.width = std::max(kMinColumnWidth,
                            std::ceil(glyph_thickness) +
                                2 * kColumnSidePadding);
    longest_word = std::max(longest_word, metrics.length);
    columns.push_back(column);
  }

  const size_t pages = session->page_count();
  _content.footerVisible = pages > 1;
  _content.canPageBackward = session->current_page() > 0;
  _content.canPageForward = session->current_page() + 1 < pages;
  _content.pageLabel =
      pages > 1 ? [NSString stringWithFormat:@"%lu/%lu",
                                             static_cast<unsigned long>(
                                                 session->current_page() + 1),
                                             static_cast<unsigned long>(pages)]
                : @"";
  _content.textBandHeight =
      std::min(longest_word + kTextBandPadding, kMaxTextBandHeight);

  CGFloat columns_width = 0;
  for (const PanelColumn& column : columns) columns_width += column.width;
  _content.columns = std::move(columns);

  // The paging footer only appears with more than one page, so a single
  // page stays as tight as possible (same rule as Windows).
  const CGFloat footer_min_width =
      kPageLabelWidth + 2 * kPageButtonWidth + 12 + 2 * kPadding;
  CGFloat width = columns_width + 2 * kPadding;
  if (_content.footerVisible) width = std::max(width, footer_min_width);
  const CGFloat height = kPadding + kNumberBandHeight +
                         _content.textBandHeight + kPadding +
                         (_content.footerVisible ? kFooterHeight : 0);

  NSRect frame = self.frame;
  frame.size = NSMakeSize(width, height);
  [self setFrame:frame display:YES];
  _content.frame = NSMakeRect(0, 0, width, height);
  _content.needsDisplay = YES;
  [self invalidateShadow];
}

- (void)showNearRect:(NSRect)caretRect {
  NSScreen* screen = [NSScreen mainScreen];
  if (!NSIsEmptyRect(caretRect)) {
    for (NSScreen* candidate in [NSScreen screens]) {
      if (NSIntersectsRect(candidate.frame, caretRect)) {
        screen = candidate;
        break;
      }
    }
  }

  NSPoint origin;
  if (NSIsEmptyRect(caretRect)) {
    origin = NSMakePoint(NSMidX(screen.frame) - self.frame.size.width / 2,
                         NSMidY(screen.frame));
  } else {
    // Right of the caret, top-aligned with the line: vertical Mongolian
    // advances in columns to the right (macOS y grows upwards).
    origin = NSMakePoint(NSMaxX(caretRect) + 4,
                         NSMaxY(caretRect) - self.frame.size.height);
  }

  // Clamp to the visible area of the caret's screen; flip to the left of
  // the caret when the right side has no room.
  const NSRect visible = screen.visibleFrame;
  if (origin.x + self.frame.size.width > NSMaxX(visible)) {
    origin.x = caretRect.origin.x - 4 - self.frame.size.width;
    if (origin.x < visible.origin.x) {
      origin.x = NSMaxX(visible) - self.frame.size.width;
    }
  }
  origin.x = std::max(origin.x, visible.origin.x);
  origin.y = std::clamp(origin.y, visible.origin.y,
                        NSMaxY(visible) - self.frame.size.height);

  [self setFrameOrigin:origin];
  [self orderFront:nil];
}

- (void)hidePanel {
  [self orderOut:nil];
}

@end
