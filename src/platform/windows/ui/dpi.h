// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Per-monitor DPI helpers. The IME popups are created inside arbitrary
// host processes whose DPI awareness we do not control; creating and
// positioning our windows under an explicit PER_MONITOR_AWARE_V2 thread
// context makes them render at native pixels (no bitmap-stretch blur)
// regardless of the host.

#ifndef PAIZA_UI_DPI_H_
#define PAIZA_UI_DPI_H_

#include <windows.h>

namespace paiza::ui {

// RAII: switches the calling thread to Per-Monitor V2 DPI awareness for
// the current scope. No-op when the process/thread is already PMv2.
class ScopedPerMonitorDpi {
 public:
  ScopedPerMonitorDpi()
      : previous_(::SetThreadDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
  ~ScopedPerMonitorDpi() {
    if (previous_ != nullptr) ::SetThreadDpiAwarenessContext(previous_);
  }
  ScopedPerMonitorDpi(const ScopedPerMonitorDpi&) = delete;
  ScopedPerMonitorDpi& operator=(const ScopedPerMonitorDpi&) = delete;

 private:
  DPI_AWARENESS_CONTEXT previous_;
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_DPI_H_
