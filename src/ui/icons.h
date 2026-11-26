#pragma once
// Vigil UI — vector iconography and the Vigil mark. Icons are drawn as geometry on a 20-unit
// grid with round-capped strokes so they stay crisp at any scale. No emoji, no bitmaps.

#include "gfx.h"

#include <vector>

namespace vigil::ui {

enum class Icon {
    // Navigation.
    Overview, Players, Checks, Utilities, Settings, Status, Evidence, Identity,
    // Check categories and checks.
    Combat, Movement, World,
    KillAura, Reach, AutoClicker, Velocity, AutoBlock, Speed, BHop, NoSlow, KeepY, FlatScaffold, LegitScaff,
    // Utilities.
    Chat, Shop, Party, Queue,
    // General.
    Search, Info, Reset, ChevronRight, ChevronDown, ChevronLeft, Back, ArrowRight, Close, Check, Warning, Clock, Pin, Export,
    Folder, Bell, Palette, Database, Pulse, Target, Filter, Plus, Minus, Dot, Bolt, Spark, Report, Layers, Keyboard, Copy,
    Eye, TrendUp, TrendDown, Globe, Play, Plug, Chip, Mask,
    // Investigation marks and relevance.
    Shield, ShieldCheck, Bot, History,
    // Records and actions (1.0-dev.9). Export = tray with an arrow out; Import = arrow in.
    Records, Save, Import, Exempt, UserPlus, Sound, More, Trash, Edit, Accessibility,
};

void drawIcon(Renderer& r, Icon icon, float x, float y, float size, Color c, float stroke = 1.6f);

// The Vigil mark: a geometric V of constant arm width whose inner notch is an arc concentric with
// a point of light — the lower lid of an attentive eye cradling the vigil flame.
// `size` is the edge of the mark's bounding square.
void drawLogo(Renderer& r, float x, float y, float size, Color body, Color dot);
// Geometry of the mark in a unit square (shared by the renderer and tools/make_brand_assets.py,
// which implements the same construction).
std::vector<Point> logoOutline(int arcSegments = 28);
Point logoDotCenter();
float logoDotRadius();

}  // namespace vigil::ui
