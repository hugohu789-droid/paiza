// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Event base class and concrete event types routed by EventDispatcher.

#ifndef PAIZA_UI_EVENT_H_
#define PAIZA_UI_EVENT_H_

#include "ui/geometry.h"

namespace paiza::ui {

enum class EventType {
  kMouseEnter,
  kMouseLeave,
  kMouseMove,
  kMouseDown,
  kMouseUp,
  kClick,  // Synthesized: mouse released over the widget that was pressed.
};

// Base class for all events flowing through the dispatcher. Subclass to add
// payload (keyboard, touch, ... in the future).
class Event {
 public:
  explicit Event(EventType type) : type_(type) {}
  virtual ~Event() = default;

  EventType type() const { return type_; }
  bool handled() const { return handled_; }
  void set_handled() { handled_ = true; }

 private:
  EventType type_;
  bool handled_ = false;
};

class MouseEvent : public Event {
 public:
  MouseEvent(EventType type, Point position)
      : Event(type), position_(position) {}

  // Position in window client coordinates.
  Point position() const { return position_; }

 private:
  Point position_;
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_EVENT_H_
