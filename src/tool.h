/*
    tool.h: Abstract base class for interaction tools.
*/

#pragma once

#include "common.h"

struct NVGcontext;
class Viewer;

class Tool {
public:
  enum Type { None, Brush, SingularityAttractor, SingularityScare, EdgeBrush };

  Tool(Viewer *viewer) : mViewer(viewer) {}
  virtual ~Tool() = default;

  virtual Type type() const = 0;

  virtual bool mouseButtonEvent(const Vector2i &p, int button, bool down,
                                int modifiers) {
    return false;
  }
  virtual bool mouseDragEvent(const Vector2i &p, const Vector2i &rel,
                              int button, int modifiers) {
    return false;
  }
  virtual bool mouseMotionEvent(const Vector2i &p, const Vector2i &rel,
                                int button, int modifiers) {
    return false;
  }
  virtual void draw(NVGcontext *ctx) {}

protected:
  Viewer *mViewer;
};
