// Copyright 2026 Paiza. All rights reserved.
//
// Thin Direct2D/DirectWrite wrapper handed to widgets during painting.
// DirectWrite is required (rather than GDI) because Traditional Mongolian
// needs both complex shaping and top-to-bottom reading direction.

#ifndef PAIZA_UI_RENDER_CONTEXT_H_
#define PAIZA_UI_RENDER_CONTEXT_H_

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ui/geometry.h"

namespace paiza::ui {

enum class TextOrientation {
  kHorizontal,  // Left-to-right rows.
  kVertical,    // Top-to-bottom columns, columns flow left-to-right.
};

struct TextStyle {
  std::wstring font_family = L"Noto Sans Mongolian";
  float font_size = 18.0f;
  uint32_t color = 0xFF202020;  // ARGB.
  TextOrientation orientation = TextOrientation::kHorizontal;
  bool bold = false;
  bool center_horizontal = false;
  bool center_vertical = false;
};

// One installed font family, for the font picker in the settings dialog.
struct FontFamilyInfo {
  std::wstring name;
  bool supports_mongolian = false;  // Has a glyph for U+1820.
};

class RenderContext {
 public:
  RenderContext() = default;
  ~RenderContext() = default;
  RenderContext(const RenderContext&) = delete;
  RenderContext& operator=(const RenderContext&) = delete;

  // Binds to a window; creates the render target lazily on first BeginDraw.
  void BindToWindow(HWND hwnd) { hwnd_ = hwnd; }
  void OnResize(int width, int height);

  // DPI scale (window DPI / 96). All drawing/measuring stays in logical
  // 96-DPI units; BeginDraw applies the scale as a render transform so
  // text rasterizes at native resolution.
  void set_scale(float scale) { scale_ = scale > 0.0f ? scale : 1.0f; }
  float scale() const { return scale_; }

  bool BeginDraw();
  // Returns false if the device was lost and resources were discarded (the
  // caller should simply invalidate and repaint).
  bool EndDraw();

  void Clear(uint32_t argb);
  void FillRect(const Rect& rect, uint32_t argb);
  void FillRoundedRect(const Rect& rect, float radius, uint32_t argb);
  void DrawBorder(const Rect& rect, uint32_t argb, float stroke_width = 1.0f);
  void DrawLine(Point from, Point to, uint32_t argb, float stroke_width);
  void DrawTextBlock(const std::wstring& text, const Rect& rect,
                     const TextStyle& style);
  void DrawBitmap(IWICBitmapSource* source, const Rect& dest);

  // Measures `text` laid out with `style` inside `max` bounds. Does not
  // require BeginDraw; usable for widget sizing.
  static Size MeasureText(const std::wstring& text, const TextStyle& style,
                          Size max);

  // Lists installed font families, Mongolian-capable ones first (each
  // group sorted by name). Used by the settings font picker.
  static std::vector<FontFamilyInfo> EnumerateFontFamilies();

  static IDWriteFactory* dwrite_factory();
  static ID2D1Factory* d2d_factory();
  static IWICImagingFactory* wic_factory();

 private:
  Microsoft::WRL::ComPtr<IDWriteTextFormat> CreateFormat(
      const TextStyle& style) const;
  ID2D1SolidColorBrush* GetBrush(uint32_t argb);
  void DiscardDeviceResources();

  static Microsoft::WRL::ComPtr<IDWriteTextFormat> CreateFormatStatic(
      const TextStyle& style);

  // Draws vertical Mongolian centred on its stem (the NIRUGU/vertical
  // baseline), not its ink bounding box, so every candidate's stem lands
  // on the same vertical — otherwise letters with rightward teeth/tails
  // push the box centre off the stem. Uses grayscale AA + physical pixels
  // for uniform stroke weight.
  void DrawVerticalCentredOnStem(const std::wstring& text, const Rect& rect,
                                 const TextStyle& physical_style,
                                 ID2D1SolidColorBrush* brush);

  HWND hwnd_ = nullptr;
  float scale_ = 1.0f;
  Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
  // Bitmap cache for DrawBitmap; valid for the current target only.
  Microsoft::WRL::ComPtr<ID2D1Bitmap> cached_bitmap_;
  IWICBitmapSource* cached_bitmap_source_ = nullptr;
};

inline D2D1_COLOR_F ArgbToColorF(uint32_t argb) {
  return D2D1::ColorF(((argb >> 16) & 0xFF) / 255.0f,
                      ((argb >> 8) & 0xFF) / 255.0f,
                      (argb & 0xFF) / 255.0f,
                      ((argb >> 24) & 0xFF) / 255.0f);
}

}  // namespace paiza::ui

#endif  // PAIZA_UI_RENDER_CONTEXT_H_
