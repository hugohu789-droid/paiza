// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "ui/image_view.h"

#include "common/logging.h"

using Microsoft::WRL::ComPtr;

namespace paiza::ui {

bool ImageView::LoadFromFile(const std::wstring& path) {
  IWICImagingFactory* wic = RenderContext::wic_factory();
  if (wic == nullptr) return false;

  ComPtr<IWICBitmapDecoder> decoder;
  if (FAILED(wic->CreateDecoderFromFilename(
          path.c_str(), nullptr, GENERIC_READ,
          WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf()))) {
    PAIZA_LOG_WARN(L"Failed to decode image: " + path);
    return false;
  }
  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return false;
  bitmap_source_ = frame;
  Invalidate();
  return true;
}

void ImageView::OnPaint(RenderContext& context) {
  if (bitmap_source_ == nullptr) return;
  context.DrawBitmap(bitmap_source_.Get(), bounds());
}

}  // namespace paiza::ui
