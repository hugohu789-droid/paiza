// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "ui/widget.h"

#include "ui/window.h"

namespace paiza::ui {

void Widget::Invalidate() {
  if (window_ != nullptr) window_->Invalidate();
}

}  // namespace paiza::ui
