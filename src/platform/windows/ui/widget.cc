// Copyright 2026 Paiza. All rights reserved.

#include "ui/widget.h"

#include "ui/window.h"

namespace paiza::ui {

void Widget::Invalidate() {
  if (window_ != nullptr) window_->Invalidate();
}

}  // namespace paiza::ui
