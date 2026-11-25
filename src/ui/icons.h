#pragma once
// Vigil UI — vector iconography and the Vigil mark. Icons are drawn as geometry on a 20-unit
// grid with round-capped strokes so they stay crisp at any scale. No emoji, no bitmaps.

#include "gfx.h"

#include <vector>

namespace vigil::ui {

enum class Icon {
    Overview, Checks, Evidence, Players, Settings,
    Combat, Movement, World,
    KillAura, Reach, AutoClicker, Velocity, AutoBlock, Speed, BHop, NoSlow, KeepY, FlatScaffold, LegitScaff,
    Search, Info, Reset, ChevronRight, ChevronDown, ChevronLeft, Close, Check, Warning, Clock, Pin, Export, Folder,
    Bell, Palette, Database, Pulse, Target, Filter, Plus, Minus, Dot, Bolt, Spark, Report, Layers,
};

void drawIcon(Renderer& r, Icon icon, float x, float y, float size, Color c, float stroke = 1.6f);

// The Vigil mark: a geometric V whose inner notch is a gentle arc cradling a point of focus.
// `size` is the height of the mark's bounding square.
void drawLogo(Renderer& r, float x, float y, float size, Color body, Color dot);
// Outline geometry of the mark in a unit square (used by the renderer and the SVG generator).
std::vector<Point> logoOutline(int curveSegments = 24);
Point logoDotCenter();
float logoDotRadius();

}  // namespace vigil::ui
