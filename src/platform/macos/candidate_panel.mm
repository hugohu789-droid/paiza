// Copyright 2026 Paiza. All rights reserved.

#import "candidate_panel.h"

#include <string>
#include <vector>

#include "common/string_utils.h"
#include "core/input_session.h"

namespace {

constexpr CGFloat kPadding = 8.0;
constexpr CGFloat kRowHeight = 24.0;
constexpr CGFloat kMinWidth = 120.0;

struct PanelRow {
  NSString* number;
  NSString* text;
  bool selected;
};

}  // namespace

// Draws the candidate rows. TODO(macos): replace the horizontal rows with
// CoreText vertical columns (numbers above the words, stem-centred via
// the shared fontstems.txt calibration) once running on-device.
@interface PaizaCandidateContentView : NSView
@property(nonatomic) std::vector<PanelRow> rows;
@property(nonatomic, copy) NSString* pageLabel;
@end

@implementation PaizaCandidateContentView

- (BOOL)isFlipped {
  return YES;  // Rows lay out top-down.
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  [[NSColor windowBackgroundColor] setFill];
  NSRectFill(self.bounds);

  NSDictionary* number_attributes = @{
    NSFontAttributeName : [NSFont systemFontOfSize:11],
    NSForegroundColorAttributeName : [NSColor secondaryLabelColor],
  };
  NSDictionary* text_attributes = @{
    NSFontAttributeName : [NSFont fontWithName:@"Noto Sans Mongolian"
                                          size:16]
        ?: [NSFont systemFontOfSize:16],
    NSForegroundColorAttributeName : [NSColor labelColor],
  };

  CGFloat y = kPadding;
  for (const PanelRow& row : _rows) {
    if (row.selected) {
      [[NSColor selectedContentBackgroundColor] setFill];
      NSRect highlight =
          NSMakeRect(4, y - 2, self.bounds.size.width - 8, kRowHeight);
      [[NSBezierPath bezierPathWithRoundedRect:highlight
                                       xRadius:5
                                       yRadius:5] fill];
    }
    [row.number drawAtPoint:NSMakePoint(kPadding, y + 3)
             withAttributes:number_attributes];
    [row.text drawAtPoint:NSMakePoint(kPadding + 22, y)
           withAttributes:text_attributes];
    y += kRowHeight;
  }
  if (_pageLabel.length > 0) {
    [_pageLabel drawAtPoint:NSMakePoint(kPadding, y + 2)
             withAttributes:number_attributes];
  }
}

@end

@implementation PaizaCandidatePanel {
  PaizaCandidateContentView* _content;
}

- (instancetype)init {
  self = [super initWithContentRect:NSMakeRect(0, 0, 200, 200)
                          styleMask:NSWindowStyleMaskBorderless |
                                    NSWindowStyleMaskNonactivatingPanel
                            backing:NSBackingStoreBuffered
                              defer:YES];
  if (self != nil) {
    self.level = NSPopUpMenuWindowLevel;
    self.hasShadow = YES;
    self.opaque = NO;
    // Never steal focus from the host application.
    self.becomesKeyOnlyIfNeeded = YES;
    _content = [[PaizaCandidateContentView alloc]
        initWithFrame:self.contentLayoutRect];
    self.contentView = _content;
  }
  return self;
}

- (BOOL)canBecomeKeyWindow {
  return NO;
}

- (void)updateFromSession:(paiza::InputSession*)session {
  std::vector<PanelRow> rows;
  size_t first = 0;
  size_t last = 0;
  session->PageRange(&first, &last);
  for (size_t i = first; i < last; ++i) {
    PanelRow row;
    row.number =
        [NSString stringWithFormat:@"%lu", static_cast<unsigned long>(
                                               i - first + 1)];
    const std::string utf8 =
        paiza::WideToUtf8(session->candidates()[i].text);
    row.text = [NSString stringWithUTF8String:utf8.c_str()] ?: @"";
    row.selected = i == session->selected_index();
    rows.push_back(row);
  }
  _content.rows = rows;
  if (session->page_count() > 1) {
    _content.pageLabel = [NSString
        stringWithFormat:@"%lu/%lu",
                         static_cast<unsigned long>(
                             session->current_page() + 1),
                         static_cast<unsigned long>(session->page_count())];
  } else {
    _content.pageLabel = @"";
  }

  const CGFloat footer =
      _content.pageLabel.length > 0 ? kRowHeight : 0.0;
  const CGFloat height =
      kPadding * 2 + kRowHeight * static_cast<CGFloat>(rows.size()) +
      footer;
  NSRect frame = self.frame;
  frame.size = NSMakeSize(kMinWidth, height);
  [self setFrame:frame display:YES];
  _content.frame = self.contentLayoutRect;
  [_content setNeedsDisplay:YES];
}

- (void)showNearRect:(NSRect)caretRect {
  NSPoint origin;
  if (NSIsEmptyRect(caretRect)) {
    NSScreen* screen = [NSScreen mainScreen];
    origin = NSMakePoint(NSMidX(screen.frame) - self.frame.size.width / 2,
                         NSMidY(screen.frame));
  } else {
    // Below the caret line (macOS y grows upwards).
    origin = NSMakePoint(caretRect.origin.x,
                         caretRect.origin.y - self.frame.size.height - 4);
  }
  [self setFrameOrigin:origin];
  [self orderFront:nil];
}

- (void)hidePanel {
  [self orderOut:nil];
}

@end
