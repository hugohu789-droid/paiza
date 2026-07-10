// Copyright 2026 Paiza. All rights reserved.

#include "ui/render_context.h"

#include <algorithm>
#include <mutex>
#include <optional>

#include "common/logging.h"
#include "ui/font_stem_config.h"

using Microsoft::WRL::ComPtr;

namespace paiza::ui {
namespace {

ComPtr<ID2D1Factory> g_d2d_factory;
ComPtr<IDWriteFactory> g_dwrite_factory;
ComPtr<IWICImagingFactory> g_wic_factory;
std::once_flag g_factories_once;

void CreateFactories() {
  ::D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,
                      g_d2d_factory.GetAddressOf());
  ::DWriteCreateFactory(
      DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown**>(g_dwrite_factory.GetAddressOf()));
  // WIC needs COM; host threads of a TSF text service are STA already.
  ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(g_wic_factory.GetAddressOf()));
}

}  // namespace

ID2D1Factory* RenderContext::d2d_factory() {
  std::call_once(g_factories_once, CreateFactories);
  return g_d2d_factory.Get();
}

IDWriteFactory* RenderContext::dwrite_factory() {
  std::call_once(g_factories_once, CreateFactories);
  return g_dwrite_factory.Get();
}

IWICImagingFactory* RenderContext::wic_factory() {
  std::call_once(g_factories_once, CreateFactories);
  return g_wic_factory.Get();
}

void RenderContext::OnResize(int width, int height) {
  if (target_ != nullptr && width > 0 && height > 0) {
    target_->Resize(D2D1::SizeU(static_cast<UINT32>(width),
                                static_cast<UINT32>(height)));
  }
}

bool RenderContext::BeginDraw() {
  if (hwnd_ == nullptr) return false;
  if (target_ == nullptr) {
    ID2D1Factory* factory = d2d_factory();
    if (factory == nullptr) return false;
    RECT rc = {};
    ::GetClientRect(hwnd_, &rc);
    const D2D1_SIZE_U size =
        D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left),
                    static_cast<UINT32>(rc.bottom - rc.top));
    // Force 96 DPI so 1 DIP == 1 pixel; otherwise D2D picks the system
    // DPI in DPI-aware processes and would scale on top of our own
    // transform (double scaling).
    const HRESULT hr = factory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f),
        D2D1::HwndRenderTargetProperties(hwnd_, size),
        target_.GetAddressOf());
    if (FAILED(hr)) {
      Logf(::paiza::LogLevel::kError, L"CreateHwndRenderTarget failed: %08X",
           hr);
      return false;
    }
    // Sharp text on opaque backgrounds; the default grayscale AA on
    // premultiplied targets looks soft.
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
  }
  target_->BeginDraw();
  // Draw in logical 96-DPI units, rasterize at the window's native DPI.
  target_->SetTransform(D2D1::Matrix3x2F::Scale(scale_, scale_));
  return true;
}

bool RenderContext::EndDraw() {
  if (target_ == nullptr) return false;
  const HRESULT hr = target_->EndDraw();
  if (hr == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
    DiscardDeviceResources();
    return false;
  }
  return SUCCEEDED(hr);
}

void RenderContext::DiscardDeviceResources() {
  brush_.Reset();
  cached_bitmap_.Reset();
  cached_bitmap_source_ = nullptr;
  target_.Reset();
}

ID2D1SolidColorBrush* RenderContext::GetBrush(uint32_t argb) {
  if (target_ == nullptr) return nullptr;
  if (brush_ == nullptr) {
    target_->CreateSolidColorBrush(ArgbToColorF(argb),
                                   brush_.GetAddressOf());
  } else {
    brush_->SetColor(ArgbToColorF(argb));
  }
  return brush_.Get();
}

void RenderContext::Clear(uint32_t argb) {
  if (target_ != nullptr) target_->Clear(ArgbToColorF(argb));
}

void RenderContext::FillRect(const Rect& rect, uint32_t argb) {
  ID2D1SolidColorBrush* brush = GetBrush(argb);
  if (brush == nullptr) return;
  target_->FillRectangle(
      D2D1::RectF(static_cast<float>(rect.x), static_cast<float>(rect.y),
                  static_cast<float>(rect.right()),
                  static_cast<float>(rect.bottom())),
      brush);
}

void RenderContext::FillRoundedRect(const Rect& rect, float radius,
                                    uint32_t argb) {
  ID2D1SolidColorBrush* brush = GetBrush(argb);
  if (brush == nullptr) return;
  target_->FillRoundedRectangle(
      D2D1::RoundedRect(
          D2D1::RectF(static_cast<float>(rect.x), static_cast<float>(rect.y),
                      static_cast<float>(rect.right()),
                      static_cast<float>(rect.bottom())),
          radius, radius),
      brush);
}

void RenderContext::DrawBorder(const Rect& rect, uint32_t argb,
                               float stroke_width) {
  ID2D1SolidColorBrush* brush = GetBrush(argb);
  if (brush == nullptr) return;
  // Offset by half the stroke so a 1px border lands on whole pixels.
  const float half = stroke_width / 2.0f;
  target_->DrawRectangle(
      D2D1::RectF(rect.x + half, rect.y + half, rect.right() - half,
                  rect.bottom() - half),
      brush, stroke_width);
}

void RenderContext::DrawLine(Point from, Point to, uint32_t argb,
                             float stroke_width) {
  ID2D1SolidColorBrush* brush = GetBrush(argb);
  if (brush == nullptr) return;
  target_->DrawLine(
      D2D1::Point2F(static_cast<float>(from.x), static_cast<float>(from.y)),
      D2D1::Point2F(static_cast<float>(to.x), static_cast<float>(to.y)),
      brush, stroke_width);
}

ComPtr<IDWriteTextFormat> RenderContext::CreateFormatStatic(
    const TextStyle& style) {
  IDWriteFactory* factory = dwrite_factory();
  if (factory == nullptr) return nullptr;
  ComPtr<IDWriteTextFormat> format;
  const HRESULT hr = factory->CreateTextFormat(
      style.font_family.c_str(), nullptr,
      style.bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, style.font_size,
      L"", format.GetAddressOf());
  if (FAILED(hr)) return nullptr;

  if (style.orientation == TextOrientation::kVertical) {
    // Traditional Mongolian: characters run top-to-bottom, successive lines
    // (columns) advance left-to-right.
    format->SetReadingDirection(DWRITE_READING_DIRECTION_TOP_TO_BOTTOM);
    format->SetFlowDirection(DWRITE_FLOW_DIRECTION_LEFT_TO_RIGHT);
  }
  if (style.center_horizontal) {
    format->SetTextAlignment(style.orientation == TextOrientation::kVertical
                                 ? DWRITE_TEXT_ALIGNMENT_LEADING
                                 : DWRITE_TEXT_ALIGNMENT_CENTER);
  }
  if (style.center_vertical || style.center_horizontal) {
    // For vertical text, paragraph alignment is the cross (horizontal) axis
    // and text alignment is the main (vertical) axis.
    if (style.orientation == TextOrientation::kVertical) {
      if (style.center_vertical) {
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
      }
      if (style.center_horizontal) {
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      }
    } else if (style.center_vertical) {
      format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
  }
  return format;
}

ComPtr<IDWriteTextFormat> RenderContext::CreateFormat(
    const TextStyle& style) const {
  return CreateFormatStatic(style);
}

void RenderContext::DrawTextBlock(const std::wstring& text, const Rect& rect,
                                  const TextStyle& style) {
  if (target_ == nullptr || text.empty()) return;

  // Draw at physical device pixels (identity transform + scaled font size)
  // rather than through the DPI scale transform. Under a scale transform
  // DirectWrite skips stem grid-fitting, so glyph stems fall on fractional
  // pixels and rasterize with uneven weight — very visible on the vertical
  // Mongolian NIRUGU stems. At the real pixel size the stems grid-fit and
  // render with uniform thickness.
  TextStyle physical = style;
  physical.font_size = style.font_size * scale_;
  ID2D1SolidColorBrush* brush = GetBrush(style.color);
  if (brush == nullptr) return;

  if (style.orientation == TextOrientation::kVertical) {
    DrawVerticalCentredOnStem(text, rect, physical, brush);
    return;
  }

  ComPtr<IDWriteTextFormat> format = CreateFormatStatic(physical);
  if (format == nullptr) return;
  target_->SetTransform(D2D1::Matrix3x2F::Identity());
  const D2D1_RECT_F physical_rect = D2D1::RectF(
      rect.x * scale_, rect.y * scale_, rect.right() * scale_,
      rect.bottom() * scale_);
  target_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()),
                     format.Get(), physical_rect, brush,
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
  target_->SetTransform(D2D1::Matrix3x2F::Scale(scale_, scale_));
}

namespace {

// Builds a leading-aligned vertical (TTB) format at the given physical size.
ComPtr<IDWriteTextFormat> CreateVerticalLeadingFormat(
    IDWriteFactory* factory, const std::wstring& family, float size,
    bool bold) {
  ComPtr<IDWriteTextFormat> format;
  if (FAILED(factory->CreateTextFormat(
          family.c_str(), nullptr,
          bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"",
          format.GetAddressOf()))) {
    return nullptr;
  }
  format->SetReadingDirection(DWRITE_READING_DIRECTION_TOP_TO_BOTTOM);
  format->SetFlowDirection(DWRITE_FLOW_DIRECTION_LEFT_TO_RIGHT);
  format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  // Cross axis (horizontal) hugs the leading edge so the vertical baseline
  // sits at a fixed offset we can measure and then re-centre on.
  format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
  return format;
}

}  // namespace

void RenderContext::DrawVerticalCentredOnStem(
    const std::wstring& text, const Rect& rect,
    const TextStyle& physical_style, ID2D1SolidColorBrush* brush) {
  IDWriteFactory* factory = dwrite_factory();
  if (factory == nullptr) return;

  const float top = rect.y * scale_;
  const float bottom = rect.bottom() * scale_;

  // ClearType's horizontal sub-pixels fringe vertical stems; grayscale
  // keeps them uniform.
  target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  target_->SetTransform(D2D1::Matrix3x2F::Identity());

  // Stem centring only for fonts with a calibration entry
  // (fontstems.txt); unknown fonts get the standard centred layout with
  // no adjustment.
  const std::optional<float> ratio =
      GetFontStemRatio(physical_style.font_family);
  if (!ratio.has_value()) {
    ComPtr<IDWriteTextFormat> format = CreateFormatStatic(physical_style);
    if (format != nullptr) {
      const D2D1_RECT_F physical_rect = D2D1::RectF(
          rect.x * scale_, top, rect.right() * scale_, bottom);
      target_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()),
                         format.Get(), physical_rect, brush,
                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
  } else {
    // Calibrated fonts use a NEAR (leading) vertical layout: all letters
    // of a word share the stem, so NEAR keeps the stems within the word
    // aligned regardless of where the box sits. The box is then placed
    // so the word's INK is centred in the column — equal margins on both
    // sides keep the columns snug (the teeth/tails are asymmetric around
    // the stem; centring the stem instead would pad the narrow side and
    // make every column look bloated).
    ComPtr<IDWriteTextFormat> format = CreateVerticalLeadingFormat(
        factory, physical_style.font_family, physical_style.font_size,
        physical_style.bold);
    ComPtr<IDWriteTextLayout> layout;
    if (format != nullptr &&
        SUCCEEDED(factory->CreateTextLayout(
            text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
            4096.0f, bottom - top, layout.GetAddressOf()))) {
      DWRITE_TEXT_METRICS metrics = {};
      if (SUCCEEDED(layout->GetMetrics(&metrics))) {
        const float column_centre =
            (rect.x + rect.width * 0.5f) * scale_;
        const float box_left =
            column_centre - (metrics.left + metrics.width * 0.5f);

        // Clip to the physical column so the word can't spill into its
        // neighbours (the column is measured for the ink width, see
        // MeasureText, so nothing visible is lost).
        const D2D1_RECT_F clip = D2D1::RectF(rect.x * scale_, top,
                                             rect.right() * scale_, bottom);
        target_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
        target_->DrawTextLayout(D2D1::Point2F(box_left, top), layout.Get(),
                                brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
        target_->PopAxisAlignedClip();
      }
    }
  }
  target_->SetTransform(D2D1::Matrix3x2F::Scale(scale_, scale_));
  target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
}

void RenderContext::DrawBitmap(IWICBitmapSource* source, const Rect& dest) {
  if (target_ == nullptr || source == nullptr) return;
  if (cached_bitmap_ == nullptr || cached_bitmap_source_ != source) {
    ComPtr<IWICFormatConverter> converter;
    IWICImagingFactory* wic = wic_factory();
    if (wic == nullptr) return;
    if (FAILED(wic->CreateFormatConverter(converter.GetAddressOf()))) return;
    if (FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
      return;
    }
    cached_bitmap_.Reset();
    if (FAILED(target_->CreateBitmapFromWicBitmap(
            converter.Get(), nullptr, cached_bitmap_.GetAddressOf()))) {
      return;
    }
    cached_bitmap_source_ = source;
  }
  target_->DrawBitmap(
      cached_bitmap_.Get(),
      D2D1::RectF(static_cast<float>(dest.x), static_cast<float>(dest.y),
                  static_cast<float>(dest.right()),
                  static_cast<float>(dest.bottom())));
}

// static
std::vector<FontFamilyInfo> RenderContext::EnumerateFontFamilies() {
  std::vector<FontFamilyInfo> families;
  IDWriteFactory* factory = dwrite_factory();
  if (factory == nullptr) return families;

  ComPtr<IDWriteFontCollection> collection;
  if (FAILED(factory->GetSystemFontCollection(collection.GetAddressOf(),
                                              FALSE))) {
    return families;
  }
  const UINT32 count = collection->GetFontFamilyCount();
  families.reserve(count);
  for (UINT32 i = 0; i < count; ++i) {
    ComPtr<IDWriteFontFamily> family;
    if (FAILED(collection->GetFontFamily(i, family.GetAddressOf()))) {
      continue;
    }
    ComPtr<IDWriteLocalizedStrings> names;
    if (FAILED(family->GetFamilyNames(names.GetAddressOf()))) continue;

    UINT32 name_index = 0;
    BOOL name_exists = FALSE;
    names->FindLocaleName(L"en-us", &name_index, &name_exists);
    if (!name_exists) name_index = 0;
    UINT32 length = 0;
    if (FAILED(names->GetStringLength(name_index, &length)) || length == 0) {
      continue;
    }
    std::vector<wchar_t> buffer(length + 1, L'\0');
    if (FAILED(names->GetString(name_index, buffer.data(),
                                static_cast<UINT32>(buffer.size())))) {
      continue;
    }

    FontFamilyInfo info;
    info.name.assign(buffer.data(), length);
    ComPtr<IDWriteFont> font;
    if (SUCCEEDED(family->GetFont(0, font.GetAddressOf()))) {
      BOOL has = FALSE;
      // U+1820 MONGOLIAN LETTER A marks Mongolian script support.
      if (SUCCEEDED(font->HasCharacter(0x1820, &has))) {
        info.supports_mongolian = has != FALSE;
      }
    }
    families.push_back(std::move(info));
  }

  std::sort(families.begin(), families.end(),
            [](const FontFamilyInfo& a, const FontFamilyInfo& b) {
              if (a.supports_mongolian != b.supports_mongolian) {
                return a.supports_mongolian;  // Mongolian-capable first.
              }
              return ::_wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
            });
  return families;
}

Size RenderContext::MeasureText(const std::wstring& text,
                                const TextStyle& style, Size max) {
  IDWriteFactory* factory = dwrite_factory();
  if (factory == nullptr) return {};

  if (style.orientation == TextOrientation::kVertical) {
    const std::optional<float> ratio = GetFontStemRatio(style.font_family);
    if (ratio.has_value()) {
      // Ink width: the display centres the ink in the column (see
      // DrawVerticalCentredOnStem), so the column only needs the actual
      // glyph extent — keeps the candidate columns snug.
      ComPtr<IDWriteTextFormat> format = CreateVerticalLeadingFormat(
          factory, style.font_family, style.font_size, style.bold);
      if (format == nullptr) return {};
      ComPtr<IDWriteTextLayout> layout;
      if (FAILED(factory->CreateTextLayout(
              text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
              4096.0f, 4096.0f, layout.GetAddressOf()))) {
        return {};
      }
      DWRITE_TEXT_METRICS metrics = {};
      if (FAILED(layout->GetMetrics(&metrics))) return {};
      return {static_cast<int>(metrics.width + 0.999f),
              static_cast<int>(metrics.height + 0.999f)};
    }
    // Fall through: unconfigured fonts measure like any other text.
  }

  ComPtr<IDWriteTextFormat> format = CreateFormatStatic(style);
  if (format == nullptr) return {};
  ComPtr<IDWriteTextLayout> layout;
  if (FAILED(factory->CreateTextLayout(
          text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
          static_cast<float>(max.width), static_cast<float>(max.height),
          layout.GetAddressOf()))) {
    return {};
  }
  DWRITE_TEXT_METRICS metrics = {};
  if (FAILED(layout->GetMetrics(&metrics))) return {};
  return {static_cast<int>(metrics.width + 0.999f),
          static_cast<int>(metrics.height + 0.999f)};
}

}  // namespace paiza::ui
