// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "ui/event_dispatcher.h"

#include "ui/widget.h"

namespace paiza::ui {

Widget* EventDispatcher::HitTest(Point position) const {
  if (widgets_ == nullptr) return nullptr;
  // Front-most widget wins: iterate in reverse paint order.
  for (auto it = widgets_->rbegin(); it != widgets_->rend(); ++it) {
    if ((*it)->HitTest(position)) return it->get();
  }
  return nullptr;
}

void EventDispatcher::SendEvent(Widget* target, EventType type,
                                Point position) {
  if (target == nullptr) return;
  MouseEvent event(type, position);
  target->HandleEvent(event);
}

void EventDispatcher::OnMouseMove(Point position) {
  Widget* target = captured_ != nullptr ? captured_ : HitTest(position);
  Widget* hover_target = HitTest(position);
  if (hover_target != hovered_) {
    SendEvent(hovered_, EventType::kMouseLeave, position);
    hovered_ = hover_target;
    SendEvent(hovered_, EventType::kMouseEnter, position);
  }
  SendEvent(target, EventType::kMouseMove, position);
}

void EventDispatcher::OnMouseDown(Point position) {
  Widget* target = HitTest(position);
  captured_ = target;
  SendEvent(target, EventType::kMouseDown, position);
}

void EventDispatcher::OnMouseUp(Point position) {
  Widget* released_over = HitTest(position);
  Widget* target = captured_ != nullptr ? captured_ : released_over;
  SendEvent(target, EventType::kMouseUp, position);
  if (captured_ != nullptr && captured_ == released_over) {
    SendEvent(captured_, EventType::kClick, position);
  }
  captured_ = nullptr;
}

void EventDispatcher::OnMouseLeaveWindow() {
  SendEvent(hovered_, EventType::kMouseLeave, Point{-1, -1});
  hovered_ = nullptr;
}

void EventDispatcher::Reset() {
  hovered_ = nullptr;
  captured_ = nullptr;
}

}  // namespace paiza::ui
