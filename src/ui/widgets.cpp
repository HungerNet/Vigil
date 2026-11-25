#include "widgets.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vigil::ui {

namespace {
float g_motion = 1.f;
}
float motionLevel() { return g_motion; }
void setMotionLevel(float m) { g_motion = std::clamp(m, 0.f, 1.f); }

Id hashId(std::string_view s, Id seed) {
    Id h = seed;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

// ── Context ──────────────────────────────────────────────────────────────────────────────────

void Ui::beginFrame(double nowSeconds) {
    dt = static_cast<float>(std::clamp(nowSeconds - time, 0.0, 0.1));
    if (time == 0.0) dt = 1.f / 60.f;
    time = nowSeconds;
    cursor = Cursor::Arrow;
    hot = 0;
    overlays.clear();
    tip_.requested = false;
}

void Ui::endFrame() {
    drawingOverlay = true;
    for (auto& fn : overlays) fn();
    drawingOverlay = false;
    drawTooltip();
    if (in.released[0] || !in.down[0]) {
        if (!in.down[0]) active = 0;
    }
    for (int i = 0; i < 3; ++i) { in.pressed[i] = false; in.released[i] = false; }
    in.wheel = 0.f;
    in.chars.clear();
    in.keys.clear();
}

bool Ui::hover(const Rect& rc) const {
    const float mx = in.mx - r.offsetX(), my = in.my - r.offsetY();
    if (!rc.contains(mx, my)) return false;
    const Rect clip = r.clip();
    if (!clip.contains(in.mx, in.my)) return false;
    if (popup && !drawingOverlay && popupRect.contains(in.mx, in.my)) return false;
    return true;
}

bool Ui::pressable(Id id, const Rect& rc, bool* hovered, bool* held) {
    const bool h = hover(rc);
    if (h) { hot = id; cursor = Cursor::Hand; }
    if (h && in.pressed[0]) active = id;
    const bool clicked = active == id && in.released[0] && h;
    if (hovered) *hovered = h;
    if (held) *held = active == id && in.down[0];
    return clicked;
}

bool Ui::consumeKey(int vk, bool* ctrl) {
    for (auto it = in.keys.begin(); it != in.keys.end(); ++it) {
        if (it->down && it->vk == vk) {
            if (ctrl) *ctrl = it->ctrl;
            in.keys.erase(it);
            return true;
        }
    }
    return false;
}

Spring& Ui::spring(Id id) { return springs_[id]; }

float& Ui::value(Id id, float init) {
    auto it = values_.find(id);
    if (it == values_.end()) it = values_.emplace(id, init).first;
    return it->second;
}

float Ui::anim(Id id, float target, float rate) {
    auto it = values_.find(id);
    if (it == values_.end()) it = values_.emplace(id, target).first;
    it->second = approach(it->second, target, rate, dt);
    return it->second;
}

float Ui::springTo(Id id, float target, float stiffness, float damping) {
    return springs_[id].step(target, dt, stiffness, damping);
}

void Ui::tooltip(Id id, const Rect& anchor, std::string text) {
    if (text.empty() || !hover(anchor)) return;
    if (tip_.id != id) {
        tip_.id = id;
        tip_.since = time;
        tip_.alpha = 0.f;
    }
    tip_.text = std::move(text);
    tip_.anchor = anchor.offset(r.offsetX(), r.offsetY());
    tip_.requested = true;
}

void Ui::drawTooltip() {
    const bool show = tip_.requested && time - tip_.since > 0.35;
    tip_.alpha = approach(tip_.alpha, show ? 1.f : 0.f, show ? 16.f : 22.f, dt);
    if (!tip_.requested && tip_.alpha < 0.02f) { tip_.id = 0; return; }
    if (tip_.alpha < 0.01f) return;
    const Theme& t = th();
    const float maxW = 300.f;
    const auto lines = fonts.wrap(type::Small, tip_.text, maxW - 24.f);
    float w = 0.f;
    for (const auto& l : lines) w = std::max(w, fonts.width(type::Small, l));
    w += 24.f;
    const float lh = fonts.lineHeight(type::Small) + 3.f;
    const float h = lines.size() * lh + 17.f;
    float x = tip_.anchor.cx() - w * 0.5f;
    float y = tip_.anchor.b() + 8.f;
    if (y + h > r.height() - 8.f) y = tip_.anchor.y - h - 8.f;
    x = std::clamp(x, 8.f, std::max(8.f, r.width() - w - 8.f));
    const float rise = (1.f - tip_.alpha) * 4.f * motionLevel();
    r.pushClip({0, 0, r.width(), r.height()});
    r.pushAlpha(tip_.alpha);
    const Rect box{x, y + rise, w, h};
    r.shadow(box, 10.f, 18.f, Color(0, 0, 0, t.light ? 0.16f : 0.45f), 6.f);
    r.roundRect(box, 10.f, t.light ? Color::hex(0x1A1F2B) : t.surfaceRaised.lighter(0.04f));
    r.roundRectStroke(box, 10.f, 1.f, t.light ? Color(1, 1, 1, 0.06f) : t.borderStrong);
    float ty = box.y + 9.f;
    const Color tc = t.light ? Color::hex(0xEEF1F6) : t.text;
    for (const auto& l : lines) {
        fonts.draw(r, type::Small, box.x + 12.f, ty, l, tc);
        ty += lh;
    }
    r.popAlpha();
    r.popClip();
}

// ── Formatting ───────────────────────────────────────────────────────────────────────────────

std::string formatNumber(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", std::clamp(decimals, 0, 4), v);
    return buf;
}

std::string formatSettingValue(const Setting& s, double v) {
    std::string out;
    if (s.type == SettingType::Int) out = formatNumber(v, 0);
    else out = formatNumber(v, s.range.decimals);
    if (s.range.unit && s.range.unit[0]) {
        const std::string u = s.range.unit;
        if (u == "%" || u == "°" || u == "x") out += u;
        else out += " " + u;
    }
    return out;
}

std::string formatDuration(double seconds) {
    seconds = std::max(0.0, seconds);
    const long long s = static_cast<long long>(seconds);
    char buf[32];
    if (s < 60) std::snprintf(buf, sizeof(buf), "%llds", s);
    else if (s < 3600) std::snprintf(buf, sizeof(buf), "%lldm %02llds", s / 60, s % 60);
    else std::snprintf(buf, sizeof(buf), "%lldh %02lldm", s / 3600, (s / 60) % 60);
    return buf;
}

std::string formatClock(double seconds) {
    seconds = std::max(0.0, seconds);
    const long long s = static_cast<long long>(seconds);
    char buf[32];
    if (s >= 3600) std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(buf, sizeof(buf), "%lld:%02lld", s / 60, s % 60);
    return buf;
}

std::string keyName(int vk) {
    switch (vk) {
        case VK_RSHIFT: return "Right Shift";
        case VK_LSHIFT: return "Left Shift";
        case VK_RCONTROL: return "Right Ctrl";
        case VK_LCONTROL: return "Left Ctrl";
        case VK_RMENU: return "Right Alt";
        case VK_LMENU: return "Left Alt";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_PRIOR: return "Page Up";
        case VK_NEXT: return "Page Down";
        case 0: return "None";
        default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    UINT scan = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    char name[64] = {};
    if (scan && GetKeyNameTextA(static_cast<LONG>(scan << 16), name, sizeof(name)) > 0) return name;
    return "Key " + std::to_string(vk);
}

// ── Drawing helpers ──────────────────────────────────────────────────────────────────────────

void card(Ui& ui, const Rect& rc, bool raised, float radius) {
    const Theme& t = ui.th();
    const float rad = radius >= 0.f ? radius : t.radiusCard;
    if (t.shadowStrength > 0.f) {
        ui.r.shadow(rc, rad, raised ? 28.f : 16.f, Color(0, 0, 0, (t.light ? 0.08f : 0.28f) * t.shadowStrength), raised ? 10.f : 4.f);
    }
    const Color base = raised ? t.surfaceRaised : t.surface;
    ui.r.roundRectGradient(rc, rad, base.lighter(t.light ? 0.f : 0.015f), base);
    ui.r.roundRectStroke(rc, rad, 1.f, t.border);
    if (t.highlight.a > 0.f) {
        ui.r.pushClip({rc.x, rc.y, rc.w, 2.f});
        ui.r.roundRectStroke(rc, rad, 1.f, t.highlight);
        ui.r.popClip();
    }
}

void divider(Ui& ui, float x0, float x1, float y) { ui.r.rect({x0, y, x1 - x0, 1.f}, ui.th().divider); }

void sectionHeader(Ui& ui, float x, float y, std::string_view title, std::string_view subtitle) {
    const Theme& t = ui.th();
    ui.fonts.draw(ui.r, type::Overline, x, y, std::string(title), t.textFaint);
    if (!subtitle.empty()) {
        const float w = ui.fonts.width(type::Overline, std::string(title));
        ui.fonts.draw(ui.r, type::Caption, x + w + 10.f, y - 1.f, std::string(subtitle), t.textFaint.alpha(0.8f));
    }
}

float chipWidth(Ui& ui, std::string_view text) { return ui.fonts.width(type::Caption, text) + 18.f; }

void chip(Ui& ui, const Rect& rc, std::string_view text, Color c, bool filled) {
    ui.r.roundRect(rc, rc.h * 0.5f, filled ? c : c.withAlpha(ui.th().light ? 0.12f : 0.14f));
    const Color tc = filled ? ui.th().textOnAccent : (ui.th().light ? c.darker(0.15f) : c.lighter(0.12f));
    ui.fonts.draw(ui.r, type::Caption, rc.cx(), rc.cy() - ui.fonts.lineHeight(type::Caption) * 0.5f, text, tc, Align::Center);
}

void statusDot(Ui& ui, float cx, float cy, Color c, bool pulse) {
    if (pulse && motionLevel() > 0.01f) {
        const float t = static_cast<float>(std::fmod(ui.time, 2.0) / 2.0);
        ui.r.circle(cx, cy, 3.5f + 7.f * ease::outCubic(t), c.alpha(0.45f * (1.f - t)));
    }
    ui.r.circle(cx, cy, 3.5f, c);
}

void confidenceRing(Ui& ui, Id id, float cx, float cy, float radius, float value, float stroke, bool label) {
    const Theme& t = ui.th();
    const float v = ui.anim(id, value, 7.f);
    ui.r.ring(cx, cy, radius, stroke, t.light ? Color(0, 0, 0, 0.07f) : Color(1, 1, 1, 0.07f));
    if (v > 0.3f) {
        const Color c = t.severity(v);
        ui.r.arc(cx, cy, radius, stroke, 0.f, 6.2831853f * std::clamp(v / 100.f, 0.f, 1.f), c, true);
    }
    if (label) {
        const TextStyle st = radius >= 22.f ? type::Label : type::Caption;
        const std::string txt = formatNumber(std::round(v), 0);
        ui.fonts.draw(ui.r, st, cx, cy - ui.fonts.lineHeight(st) * 0.5f, txt, t.text, Align::Center);
    }
}

void meter(Ui& ui, const Rect& rc, float fraction, Color c) {
    ui.r.roundRect(rc, rc.h * 0.5f, ui.th().light ? Color(0, 0, 0, 0.06f) : Color(1, 1, 1, 0.06f));
    const float f = std::clamp(fraction, 0.f, 1.f);
    if (f > 0.f) ui.r.roundRect({rc.x, rc.y, std::max(rc.h, rc.w * f), rc.h}, rc.h * 0.5f, c);
}

void emptyState(Ui& ui, const Rect& rc, Icon icon, std::string_view title, std::string_view body) {
    const Theme& t = ui.th();
    const float cy = rc.cy() - 34.f;
    ui.r.circle(rc.cx(), cy, 26.f, t.accent.withAlpha(0.08f));
    drawIcon(ui.r, icon, rc.cx() - 11.f, cy - 11.f, 22.f, t.accent.alpha(0.85f));
    ui.fonts.draw(ui.r, type::Title, rc.cx(), cy + 40.f, std::string(title), t.text, Align::Center);
    const auto lines = ui.fonts.wrap(type::Small, body, std::min(360.f, rc.w - 40.f));
    float y = cy + 66.f;
    for (const auto& l : lines) {
        ui.fonts.draw(ui.r, type::Small, rc.cx(), y, l, t.textMuted, Align::Center);
        y += ui.fonts.lineHeight(type::Small) + 3.f;
    }
}

// ── Controls ─────────────────────────────────────────────────────────────────────────────────

float buttonWidth(Ui& ui, std::string_view label, bool withIcon) {
    return ui.fonts.width(type::BodyStrong, label) + 28.f + (withIcon ? 22.f : 0.f);
}

bool button(Ui& ui, Id id, const Rect& rc, std::string_view label, ButtonKind kind, const Icon* icon) {
    const Theme& t = ui.th();
    bool h = false, held = false;
    const bool clicked = ui.pressable(id, rc, &h, &held);
    const float hv = ui.anim(mixId(id, 1), h ? 1.f : 0.f, 18.f);
    const float pr = ui.anim(mixId(id, 2), held ? 1.f : 0.f, 30.f);
    Rect b = rc.inset(pr * 0.8f);
    Color bg, fg, border{0, 0, 0, 0};
    switch (kind) {
        case ButtonKind::Primary:
            bg = t.accent.lighter(0.08f * hv);
            fg = t.textOnAccent;
            if (t.glow > 0.f) ui.r.shadow(b, t.radiusControl, 16.f, t.accent.withAlpha(0.22f * t.glow * (0.6f + 0.4f * hv)), 3.f);
            break;
        case ButtonKind::Secondary:
            bg = t.surfaceRaised.mix(t.surfaceHover, hv);
            fg = t.text;
            border = t.borderStrong;
            break;
        case ButtonKind::Ghost:
            bg = t.surfaceHover.withAlpha(0.9f * hv);
            fg = t.textMuted.mix(t.text, hv);
            break;
        case ButtonKind::Danger:
            bg = t.sevCritical.withAlpha(0.12f + 0.08f * hv);
            fg = t.light ? t.sevCritical.darker(0.1f) : t.sevCritical.lighter(0.2f);
            break;
    }
    ui.r.roundRect(b, t.radiusControl, bg);
    if (border.a > 0.f) ui.r.roundRectStroke(b, t.radiusControl, 1.f, border);
    const float lw = ui.fonts.width(type::BodyStrong, label) + (icon ? 22.f : 0.f);
    float x = b.cx() - lw * 0.5f;
    const float ty = b.cy() - ui.fonts.lineHeight(type::BodyStrong) * 0.5f;
    if (icon) {
        drawIcon(ui.r, *icon, x, b.cy() - 8.f, 16.f, fg);
        x += 22.f;
    }
    ui.fonts.draw(ui.r, type::BodyStrong, x, ty, label, fg);
    return clicked;
}

bool iconButton(Ui& ui, Id id, const Rect& rc, Icon icon, std::string_view tip, bool activeState) {
    const Theme& t = ui.th();
    bool h = false, held = false;
    const bool clicked = ui.pressable(id, rc, &h, &held);
    const float hv = ui.anim(mixId(id, 1), h || activeState ? 1.f : 0.f, 18.f);
    if (hv > 0.01f) ui.r.roundRect(rc, t.radiusControl, (activeState ? t.accentSoft() : t.surfaceHover).alpha(hv));
    const float sz = std::min(rc.w, rc.h) * 0.55f;
    const Color c = activeState ? t.accent : t.textMuted.mix(t.text, hv);
    drawIcon(ui.r, icon, rc.cx() - sz * 0.5f, rc.cy() - sz * 0.5f + (held ? 0.5f : 0.f), sz, c);
    if (!tip.empty()) ui.tooltip(id, rc, std::string(tip));
    return clicked;
}

bool toggle(Ui& ui, Id id, const Rect& rc, bool on) {
    const Theme& t = ui.th();
    bool h = false, held = false;
    const bool clicked = ui.pressable(id, rc.inset(-4.f), &h, &held);
    const float k = ui.springTo(mixId(id, 1), on ? 1.f : 0.f, 420.f, 22.f);
    const float hv = ui.anim(mixId(id, 2), h ? 1.f : 0.f, 18.f);
    const float kc = std::clamp(k, 0.f, 1.f);
    const Color off = t.light ? Color::hex(0xD5DAE3) : t.inset.lighter(0.06f);
    ui.r.roundRect(rc, rc.h * 0.5f, off.mix(t.accent, kc));
    if (kc < 0.99f) ui.r.roundRectStroke(rc, rc.h * 0.5f, 1.f, t.borderStrong.alpha(1.f - kc));
    if (hv > 0.01f) ui.r.roundRectStroke(rc.inset(-2.5f), rc.h * 0.5f + 2.5f, 1.5f, t.accent.withAlpha(0.25f * hv));
    const float pad = 3.f;
    const float d = rc.h - pad * 2.f;
    const float stretch = held ? 3.f : 0.f;
    const float travel = rc.w - pad * 2.f - d - stretch;
    const float kx = rc.x + pad + travel * k;
    const Rect knob{kx, rc.y + pad, d + stretch, d};
    ui.r.shadow(knob, d * 0.5f, 4.f, Color(0, 0, 0, 0.25f), 1.f);
    ui.r.roundRect(knob, d * 0.5f, t.light ? Color::hex(0xFFFFFF) : Color::hex(0xF4F6FA).mix(t.textOnAccent, 0.f));
    return clicked;
}

bool slider(Ui& ui, Id id, const Rect& rc, double& value, double mn, double mx, double step, double def, bool showDefault) {
    const Theme& t = ui.th();
    bool h = false, held = false;
    ui.pressable(id, rc.inset(-2.f, -6.f), &h, &held);
    if (h) ui.cursor = Cursor::Hand;
    bool changed = false;
    const double span = std::max(1e-9, mx - mn);
    if (held) {
        double f = std::clamp((ui.in.mx - ui.r.offsetX() - rc.x) / rc.w, 0.f, 1.f);
        double v = mn + f * span;
        if (step > 0.0) v = mn + std::round((v - mn) / step) * step;
        v = std::clamp(v, mn, mx);
        if (std::fabs(v - value) > 1e-12) { value = v; changed = true; }
    }
    const float f = static_cast<float>((value - mn) / span);
    const float fa = ui.anim(mixId(id, 3), f, 30.f);
    const float cy = rc.cy();
    const Rect track{rc.x, cy - 2.f, rc.w, 4.f};
    ui.r.roundRect(track, 2.f, t.light ? Color(0, 0, 0, 0.08f) : Color(1, 1, 1, 0.08f));
    ui.r.roundRect({track.x, track.y, std::max(4.f, track.w * fa), track.h}, 2.f, t.accent);
    if (showDefault) {
        const float df = static_cast<float>((def - mn) / span);
        const float dx = rc.x + rc.w * std::clamp(df, 0.f, 1.f);
        ui.r.roundRect({dx - 1.f, cy - 6.f, 2.f, 12.f}, 1.f, t.textFaint.alpha(0.7f));
    }
    const float grow = ui.springTo(mixId(id, 4), held ? 1.f : (h ? 0.55f : 0.f), 360.f, 24.f);
    const float kr = 6.5f + 2.f * grow;
    const float kx = rc.x + rc.w * fa;
    if (t.glow > 0.f) ui.r.circle(kx, cy, kr + 5.f * grow, t.accent.withAlpha(0.18f * grow));
    ui.r.shadow({kx - kr, cy - kr, kr * 2, kr * 2}, kr, 5.f, Color(0, 0, 0, 0.3f), 1.5f);
    ui.r.circle(kx, cy, kr, t.light ? Color::hex(0xFFFFFF) : Color::hex(0xF4F6FA));
    ui.r.circle(kx, cy, kr * 0.42f, t.accent);
    return changed;
}

bool segmented(Ui& ui, Id id, const Rect& rc, const std::vector<std::string>& options, int& selected) {
    const Theme& t = ui.th();
    if (options.empty()) return false;
    ui.r.roundRect(rc, t.radiusControl, t.inset);
    ui.r.roundRectStroke(rc, t.radiusControl, 1.f, t.border);
    const float segW = rc.w / options.size();
    const float sx = ui.springTo(mixId(id, 1), rc.x + segW * selected, 340.f, 30.f);
    const Rect pill{sx + 3.f, rc.y + 3.f, segW - 6.f, rc.h - 6.f};
    if (t.shadowStrength > 0.f) ui.r.shadow(pill, t.radiusControl - 2.f, 8.f, Color(0, 0, 0, 0.25f * t.shadowStrength), 2.f);
    ui.r.roundRect(pill, t.radiusControl - 2.f, t.light ? Color::hex(0xFFFFFF) : t.surfaceRaised.lighter(0.05f));
    ui.r.roundRectStroke(pill, t.radiusControl - 2.f, 1.f, t.borderStrong);
    bool changed = false;
    for (std::size_t i = 0; i < options.size(); ++i) {
        const Rect seg{rc.x + segW * i, rc.y, segW, rc.h};
        bool h = false;
        if (ui.pressable(mixId(id, 100 + i), seg, &h) && selected != static_cast<int>(i)) {
            selected = static_cast<int>(i);
            changed = true;
        }
        const bool sel = selected == static_cast<int>(i);
        const float hv = ui.anim(mixId(id, 200 + i), sel ? 1.f : (h ? 0.5f : 0.f), 16.f);
        ui.fonts.draw(ui.r, type::SmallStrong, seg.cx(), seg.cy() - ui.fonts.lineHeight(type::SmallStrong) * 0.5f, options[i],
                      t.textMuted.mix(t.text, hv), Align::Center, segW - 8.f);
    }
    return changed;
}

bool dropdown(Ui& ui, Id id, const Rect& rc, const std::vector<std::string>& options, int& selected) {
    const Theme& t = ui.th();
    bool h = false;
    const bool open = ui.popup == id;
    if (ui.pressable(id, rc, &h)) ui.popup = open ? 0 : id;
    const float hv = ui.anim(mixId(id, 1), h || open ? 1.f : 0.f, 18.f);
    ui.r.roundRect(rc, t.radiusControl, t.inset.mix(t.surfaceHover, hv * 0.6f));
    ui.r.roundRectStroke(rc, t.radiusControl, 1.f, open ? t.accent.withAlpha(0.6f) : t.borderStrong);
    const std::string cur = (selected >= 0 && selected < static_cast<int>(options.size())) ? options[static_cast<std::size_t>(selected)] : "";
    ui.fonts.draw(ui.r, type::Body, rc.x + 12.f, rc.cy() - ui.fonts.lineHeight(type::Body) * 0.5f, cur, t.text, Align::Left, rc.w - 40.f);
    const float rot = ui.anim(mixId(id, 2), open ? 1.f : 0.f, 20.f);
    drawIcon(ui.r, rot > 0.5f ? Icon::ChevronDown : Icon::ChevronRight, rc.r() - 26.f, rc.cy() - 8.f, 16.f, t.textMuted);

    bool changed = false;
    // Selections made in last frame's overlay are applied here (overlays run after this call returns).
    float& pending = ui.value(mixId(id, 600), 0.f);
    if (pending > 0.f) {
        selected = static_cast<int>(pending) - 1;
        pending = 0.f;
        changed = true;
    }
    const float openT = ui.anim(mixId(id, 3), ui.popup == id ? 1.f : 0.f, 22.f);
    if (openT > 0.01f) {
        const float itemH = 32.f;
        Rect pop{rc.x + ui.r.offsetX(), rc.b() + 6.f + ui.r.offsetY(), std::max(rc.w, 180.f), itemH * options.size() + 8.f};
        if (pop.b() > ui.r.height() - 8.f) pop.y = rc.y + ui.r.offsetY() - pop.h - 6.f;
        if (ui.popup == id) ui.popupRect = pop;
        const int current = selected;
        ui.addOverlay([&ui, id, pop, options, openT, current, itemH]() {
            const Theme& tt = ui.th();
            ui.r.pushClip({0, 0, ui.r.width(), ui.r.height()});
            ui.r.pushAlpha(openT);
            const Rect p = pop.offset(0, (1.f - openT) * -6.f * motionLevel());
            ui.r.shadow(p, 12.f, 24.f, Color(0, 0, 0, tt.light ? 0.14f : 0.45f), 8.f);
            ui.r.roundRect(p, 12.f, tt.surfaceRaised);
            ui.r.roundRectStroke(p, 12.f, 1.f, tt.borderStrong);
            for (std::size_t i = 0; i < options.size(); ++i) {
                const Rect it{p.x + 4.f, p.y + 4.f + itemH * i, p.w - 8.f, itemH};
                bool hh = false;
                if (ui.pressable(mixId(id, 500 + i), it, &hh)) {
                    ui.value(mixId(id, 600), 0.f) = static_cast<float>(i + 1);
                    ui.popup = 0;
                }
                if (hh) ui.r.roundRect(it, 8.f, tt.surfaceHover);
                const bool sel = static_cast<int>(i) == current;
                ui.fonts.draw(ui.r, type::Body, it.x + 12.f, it.cy() - ui.fonts.lineHeight(type::Body) * 0.5f, options[i],
                              sel ? tt.text : tt.textMuted);
                if (sel) drawIcon(ui.r, Icon::Check, it.r() - 26.f, it.cy() - 8.f, 16.f, tt.accent);
            }
            ui.r.popAlpha();
            ui.r.popClip();
        });
    }
    // Close when clicking elsewhere.
    if (ui.popup == id && ui.in.pressed[0] && !ui.hoverRaw(rc) && !ui.popupRect.contains(ui.in.mx, ui.in.my)) ui.popup = 0;
    return changed;
}

bool textField(Ui& ui, Id id, const Rect& rc, std::string& text, std::string_view placeholder, Icon leading) {
    const Theme& t = ui.th();
    bool h = false;
    if (ui.pressable(id, rc, &h)) ui.focus = id;
    if (h) ui.cursor = Cursor::IBeam;
    if (ui.in.pressed[0] && !h && ui.focus == id) ui.focus = 0;
    const bool focused = ui.focus == id;
    bool changed = false;
    if (focused) {
        for (unsigned int ch : ui.in.chars) {
            if (ch >= 32 && ch < 127 && text.size() < 64) { text.push_back(static_cast<char>(ch)); changed = true; }
        }
        bool ctrl = false;
        while (ui.consumeKey(VK_BACK, &ctrl)) {
            if (!text.empty()) {
                if (ctrl) text.clear(); else text.pop_back();
                changed = true;
            }
        }
        if (ui.consumeKey(VK_ESCAPE) || ui.consumeKey(VK_RETURN)) ui.focus = 0;
    }
    const float fv = ui.anim(mixId(id, 1), focused ? 1.f : 0.f, 18.f);
    ui.r.roundRect(rc, t.radiusControl, t.inset);
    ui.r.roundRectStroke(rc, t.radiusControl, 1.f, t.borderStrong.mix(t.accent.withAlpha(0.7f), fv));
    if (fv > 0.01f && t.glow > 0.f) ui.r.roundRectStroke(rc.inset(-3.f), t.radiusControl + 3.f, 2.f, t.accent.withAlpha(0.12f * fv));
    drawIcon(ui.r, leading, rc.x + 10.f, rc.cy() - 8.f, 16.f, t.textFaint.mix(t.textMuted, fv));
    const float tx = rc.x + 34.f;
    const float ty = rc.cy() - ui.fonts.lineHeight(type::Body) * 0.5f;
    if (text.empty()) ui.fonts.draw(ui.r, type::Body, tx, ty, placeholder, t.textFaint);
    const float w = ui.fonts.draw(ui.r, type::Body, tx, ty, text, t.text);
    if (focused && std::fmod(ui.time, 1.0) < 0.6) {
        ui.r.rect({tx + w + 1.f, ty + 1.f, 1.5f, ui.fonts.lineHeight(type::Body) - 2.f}, t.accent);
    }
    return changed;
}

bool keybind(Ui& ui, Id id, const Rect& rc, int& vk) {
    const Theme& t = ui.th();
    bool h = false;
    if (ui.pressable(id, rc, &h)) ui.captureKey = ui.captureKey == id ? 0 : id;
    const bool capturing = ui.captureKey == id;
    bool changed = false;
    if (capturing) {
        for (const auto& k : ui.in.keys) {
            if (!k.down) continue;
            if (k.vk != VK_ESCAPE) { vk = k.vk; changed = true; }
            ui.captureKey = 0;
            break;
        }
        ui.in.keys.clear();
    }
    const float hv = ui.anim(mixId(id, 1), h || capturing ? 1.f : 0.f, 18.f);
    ui.r.roundRect(rc, t.radiusControl, capturing ? t.accentSoft() : t.inset.mix(t.surfaceHover, hv * 0.6f));
    ui.r.roundRectStroke(rc, t.radiusControl, 1.f, capturing ? t.accent : t.borderStrong);
    const std::string label = capturing ? "Press a key\xE2\x80\xA6" : keyName(vk);
    ui.fonts.draw(ui.r, type::BodyStrong, rc.cx(), rc.cy() - ui.fonts.lineHeight(type::BodyStrong) * 0.5f, label,
                  capturing ? t.accent : t.text, Align::Center, rc.w - 12.f);
    return changed;
}

void infoIcon(Ui& ui, Id id, float x, float y, std::string text) {
    const Rect rc{x, y, 16.f, 16.f};
    const bool h = ui.hover(rc.inset(-3.f));
    const float hv = ui.anim(mixId(id, 1), h ? 1.f : 0.f, 18.f);
    drawIcon(ui.r, Icon::Info, x + 1.f, y + 1.f, 14.f, ui.th().textFaint.mix(ui.th().accent, hv), 1.4f);
    ui.tooltip(id, rc.inset(-3.f), std::move(text));
}

// ── Setting rows ─────────────────────────────────────────────────────────────────────────────

float settingRow(Ui& ui, Setting& s, float x, float y, float w, const SettingRowOptions& opt) {
    const Theme& t = ui.th();
    const Id id = hashId(s.key);
    const float rowH = 54.f;
    const bool warn = s.warningActive();
    const float warnH = warn ? 26.f : 0.f;
    const float totalH = rowH + warnH;
    const Rect row{x, y, w, totalH};
    const bool hoverRow = ui.hover(row);
    const float hv = ui.anim(mixId(id, 9), hoverRow ? 1.f : 0.f, 14.f);
    if (hv > 0.01f) ui.r.roundRect(row.inset(-8.f, 0.f), 10.f, t.surfaceHover.alpha(0.55f * hv));

    float controlW = 250.f;
    if (s.type == SettingType::Bool) controlW = 40.f;
    else if (s.type == SettingType::Choice) controlW = std::min(300.f, std::max(200.f, s.choices.size() * 78.f));
    else if (s.type == SettingType::Key) controlW = 140.f;
    const float labelW = w - controlW - 64.f;

    // Modified marker.
    const bool modified = !s.isDefault();
    const float mv = ui.anim(mixId(id, 10), modified ? 1.f : 0.f, 16.f);
    if (mv > 0.01f) ui.r.circle(x - 1.f, y + 19.f, 2.6f * mv, t.accent);

    const float labelX = x + 10.f;
    const float lw = ui.fonts.draw(ui.r, type::BodyStrong, labelX, y + 11.f, s.label, t.text, Align::Left, labelW - 26.f);
    infoIcon(ui, mixId(id, 11), labelX + lw + 7.f, y + 11.5f, s.tooltip);
    ui.fonts.draw(ui.r, type::Small, labelX, y + 30.f, s.tooltip, t.textFaint, Align::Left, labelW);

    // Control.
    const float cx = x + w - controlW;
    const float cy = y + rowH * 0.5f;
    bool edited = false;
    switch (s.type) {
        case SettingType::Bool: {
            if (toggle(ui, mixId(id, 1), {cx, cy - 11.f, 40.f, 22.f}, s.b())) { s.set(s.b() ? 0.0 : 1.0); edited = true; }
            break;
        }
        case SettingType::Int:
        case SettingType::Float: {
            double v = s.value;
            const float valueW = 74.f;
            if (slider(ui, mixId(id, 2), {cx, cy - 8.f, controlW - valueW - 12.f, 16.f}, v, s.range.min, s.range.max,
                       s.range.step, s.def)) {
                s.set(v);
                edited = true;
            }
            const Rect vr{x + w - valueW, cy - 13.f, valueW, 26.f};
            ui.r.roundRect(vr, 7.f, t.inset);
            ui.r.roundRectStroke(vr, 7.f, 1.f, t.border);
            ui.fonts.draw(ui.r, type::SmallStrong, vr.cx(), vr.cy() - ui.fonts.lineHeight(type::SmallStrong) * 0.5f,
                          formatSettingValue(s, s.value), modified ? t.accent : t.text, Align::Center, valueW - 6.f);
            break;
        }
        case SettingType::Choice: {
            int sel = s.i();
            bool ch = false;
            if (s.choices.size() <= 4) ch = segmented(ui, mixId(id, 3), {cx, cy - 15.f, controlW, 30.f}, s.choices, sel);
            else ch = dropdown(ui, mixId(id, 3), {cx, cy - 15.f, controlW, 30.f}, s.choices, sel);
            if (ch) { s.set(sel); edited = true; }
            break;
        }
        case SettingType::Key: {
            int vk = s.i();
            if (keybind(ui, mixId(id, 4), {cx, cy - 15.f, controlW, 30.f}, vk)) { s.set(vk); edited = true; }
            break;
        }
        case SettingType::Text:
            break;
    }

    // Reset to default (appears when modified).
    if (mv > 0.01f) {
        const Rect rr{cx - 36.f, cy - 14.f, 28.f, 28.f};
        ui.r.pushAlpha(mv);
        std::string def = s.type == SettingType::Bool ? (s.def != 0.0 ? "On" : "Off")
                        : s.type == SettingType::Choice ? (s.def >= 0 && s.def < s.choices.size() ? s.choices[static_cast<std::size_t>(s.def)] : "")
                        : s.type == SettingType::Key ? keyName(static_cast<int>(s.def))
                        : formatSettingValue(s, s.def);
        if (iconButton(ui, mixId(id, 5), rr, Icon::Reset, "Reset to default \xC2\xB7 " + def) && modified) {
            s.reset();
            edited = true;
        }
        ui.r.popAlpha();
    }

    if (warn) {
        const float wy = y + rowH - 4.f;
        drawIcon(ui.r, Icon::Warning, labelX, wy, 14.f, t.sevNotice, 1.5f);
        ui.fonts.draw(ui.r, type::Small, labelX + 20.f, wy, s.warning, t.light ? t.sevNotice.darker(0.2f) : t.sevNotice,
                      Align::Left, w - 30.f);
    }
    if (edited && opt.onEdited) opt.onEdited(s);
    return totalH;
}

// ── Scrolling ────────────────────────────────────────────────────────────────────────────────

ScrollState& scrollState(Ui& ui, Id id) {
    static std::unordered_map<Id, ScrollState> states;
    (void)ui;
    return states[id];
}

float beginScroll(Ui& ui, Id id, const Rect& view) {
    ScrollState& st = scrollState(ui, id);
    st.view = view.h;
    if (ui.hover(view) && ui.in.wheel != 0.f && !ui.popup) {
        st.target -= ui.in.wheel * 80.f;
        ui.in.wheel = 0.f;
    }
    const float maxScroll = std::max(0.f, st.content - st.view);
    st.target = std::clamp(st.target, 0.f, maxScroll);
    const float pos = st.pos.step(st.target, ui.dt, 300.f, 32.f);
    ui.r.pushClip(view);
    return -pos;
}

void endScroll(Ui& ui, Id id, const Rect& view, float contentHeight) {
    ScrollState& st = scrollState(ui, id);
    st.content = contentHeight;
    ui.r.popClip();
    if (contentHeight <= view.h + 1.f) return;
    const Theme& t = ui.th();
    const float vis = ui.anim(mixId(id, 77), ui.hover(view) ? 1.f : 0.35f, 8.f);
    const float ratio = view.h / contentHeight;
    const float barH = std::max(28.f, view.h * ratio);
    const float maxScroll = contentHeight - view.h;
    const float f = maxScroll > 0.f ? std::clamp(st.pos.x / maxScroll, 0.f, 1.f) : 0.f;
    const Rect bar{view.r() - 5.f, view.y + 3.f + (view.h - barH - 6.f) * f, 3.f, barH};
    ui.r.roundRect(bar, 1.5f, (t.light ? Color(0, 0, 0, 0.25f) : Color(1, 1, 1, 0.22f)).alpha(vis));
}

void scrollTo(Ui& ui, Id id, float y) { scrollState(ui, id).target = y; }

}  // namespace vigil::ui
