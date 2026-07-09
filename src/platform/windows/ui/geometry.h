// Copyright 2026 Paiza. All rights reserved.
//
// Plain value types shared by the widget toolkit. Pixel units.

#ifndef PAIZA_UI_GEOMETRY_H_
#define PAIZA_UI_GEOMETRY_H_

namespace paiza::ui {

struct Point {
  int x = 0;
  int y = 0;
};

struct Size {
  int width = 0;
  int height = 0;
};

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  int right() const { return x + width; }
  int bottom() const { return y + height; }

  bool Contains(Point p) const {
    return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
  }

  Rect Inflated(int dx, int dy) const {
    return {x - dx, y - dy, width + 2 * dx, height + 2 * dy};
  }
};

}  // namespace paiza::ui

#endif  // PAIZA_UI_GEOMETRY_H_
