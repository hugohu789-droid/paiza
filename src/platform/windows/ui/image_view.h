// Copyright 2026 Paiza. All rights reserved.
//
// Displays a bitmap loaded through WIC (PNG/JPG/BMP/ICO...).

#ifndef PAIZA_UI_IMAGE_VIEW_H_
#define PAIZA_UI_IMAGE_VIEW_H_

#include <string>

#include "ui/widget.h"

namespace paiza::ui {

class ImageView : public Widget {
 public:
  ImageView() = default;

  // Loads an image file; returns false if the file cannot be decoded.
  bool LoadFromFile(const std::wstring& path);
  bool has_image() const { return bitmap_source_ != nullptr; }

  // Widget:
  void OnPaint(RenderContext& context) override;

 private:
  Microsoft::WRL::ComPtr<IWICBitmapSource> bitmap_source_;
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_IMAGE_VIEW_H_
