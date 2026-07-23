// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Routes window mouse input to widgets: hover enter/leave tracking, mouse
// capture while a button is held, and click synthesis.

#ifndef PAIZA_UI_EVENT_DISPATCHER_H_
#define PAIZA_UI_EVENT_DISPATCHER_H_

#include <memory>
#include <vector>

#include "ui/event.h"
#include "ui/geometry.h"

namespace paiza::ui {

class Widget;

class EventDispatcher {
 public:
  EventDispatcher() = default;
  EventDispatcher(const EventDispatcher&) = delete;
  EventDispatcher& operator=(const EventDispatcher&) = delete;

  // The window shares its widget list (ordered back-to-front).
  void SetWidgets(const std::vector<std::unique_ptr<Widget>>* widgets) {
    widgets_ = widgets;
  }

  void OnMouseMove(Point position);
  void OnMouseDown(Point position);
  void OnMouseUp(Point position);
  void OnMouseLeaveWindow();

  // Drops any dangling pointers (call when widgets are removed).
  void Reset();

 private:
  Widget* HitTest(Point position) const;
  void SendEvent(Widget* target, EventType type, Point position);

  const std::vector<std::unique_ptr<Widget>>* widgets_ = nullptr;
  Widget* hovered_ = nullptr;
  Widget* captured_ = nullptr;
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_EVENT_DISPATCHER_H_
