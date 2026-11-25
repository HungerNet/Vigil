#pragma once
// Vigil UI — immediate-mode widget toolkit with retained animation state.

#include "../core/settings.h"
#include "anim.h"
#include "app.h"
#include "font.h"
#include "gfx.h"
#include "icons.h"
#include "theme.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vigil::ui {

using Id = std::uint64_t;
Id hashId(std::string_view s, Id seed = 1469598103934665603ull);
inline Id mixId(Id a, Id b) { return (a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2))); }

struct InputState {
    float mx = -1e5f, my = -1e5f;
    bool down[3] = {};
    bool pressed[3] = {};
    bool released[3] = {};
    float wheel = 0.f;
    std::vector<unsigned int> chars;
    struct Key { int vk; bool down; bool ctrl; bool shift; };
    std::vector<Key> keys;
};

class Ui {
public:
    Renderer r;
    Fonts fonts;
    InputState in;
    double time = 0.0;   // seconds
    float dt = 1.f / 60.f;
    Cursor cursor = Cursor::Arrow;

    Id hot = 0, active = 0, focus = 0;
    Id captureKey = 0;      // keybind widget waiting for a key

    // Popup (dropdown) — only one at a time.
    Id popup = 0;
    Rect popupRect;
    bool drawingOverlay = false;
    std::vector<std::function<void()>> overlays;

    const Theme& th() const { return theme(); }
    float s(float v) const { return v; }  // layout is in dp already; hook for future density tweaks

    void beginFrame(double nowSeconds);
    void endFrame();

    bool hover(const Rect& rc) const;
    bool hoverRaw(const Rect& rc) const { return rc.contains(in.mx - r.offsetX(), in.my - r.offsetY()); }
    // Standard press/release behavior. Returns true on click.
    bool pressable(Id id, const Rect& rc, bool* hovered = nullptr, bool* held = nullptr);
    bool consumeKey(int vk, bool* ctrl = nullptr);

    Spring& spring(Id id);
    float& value(Id id, float init = 0.f);
    float anim(Id id, float target, float rate = 14.f);   // exponential approach helper
    float springTo(Id id, float target, float stiffness = 260.f, float damping = 26.f);

    void tooltip(Id id, const Rect& anchor, std::string text);
    void drawTooltip();
    void addOverlay(std::function<void()> fn) { overlays.push_back(std::move(fn)); }

private:
    struct Tip { Id id = 0; std::string text; Rect anchor; double since = 0; float alpha = 0.f; bool requested = false; };
    Tip tip_;
    std::unordered_map<Id, Spring> springs_;
    std::unordered_map<Id, float> values_;
};

// ── Drawing helpers ──────────────────────────────────────────────────────────────────────────
void card(Ui& ui, const Rect& rc, bool raised = false, float radius = -1.f);
void divider(Ui& ui, float x0, float x1, float y);
void sectionHeader(Ui& ui, float x, float y, std::string_view title, std::string_view subtitle = {});
void chip(Ui& ui, const Rect& rc, std::string_view text, Color c, bool filled = false);
float chipWidth(Ui& ui, std::string_view text);
void statusDot(Ui& ui, float cx, float cy, Color c, bool pulse);
void confidenceRing(Ui& ui, Id id, float cx, float cy, float radius, float value, float stroke = 4.f, bool label = true);
void meter(Ui& ui, const Rect& rc, float fraction, Color c);
void emptyState(Ui& ui, const Rect& rc, Icon icon, std::string_view title, std::string_view body);
std::string formatNumber(double v, int decimals);
std::string formatSettingValue(const Setting& s, double v);
std::string formatDuration(double seconds);
std::string formatClock(double seconds);   // m:ss or h:mm:ss
std::string keyName(int vk);

// ── Controls ─────────────────────────────────────────────────────────────────────────────────
enum class ButtonKind { Primary, Secondary, Ghost, Danger };
bool button(Ui& ui, Id id, const Rect& rc, std::string_view label, ButtonKind kind = ButtonKind::Secondary,
            const Icon* icon = nullptr);
float buttonWidth(Ui& ui, std::string_view label, bool withIcon = false);
bool iconButton(Ui& ui, Id id, const Rect& rc, Icon icon, std::string_view tip = {}, bool active = false);
bool toggle(Ui& ui, Id id, const Rect& rc, bool on);   // returns true when clicked
bool slider(Ui& ui, Id id, const Rect& rc, double& value, double min, double max, double step, double def,
            bool showDefault = true);
bool segmented(Ui& ui, Id id, const Rect& rc, const std::vector<std::string>& options, int& selected);
bool dropdown(Ui& ui, Id id, const Rect& rc, const std::vector<std::string>& options, int& selected);
bool textField(Ui& ui, Id id, const Rect& rc, std::string& text, std::string_view placeholder, Icon leading = Icon::Search);
bool keybind(Ui& ui, Id id, const Rect& rc, int& vk);
void infoIcon(Ui& ui, Id id, float x, float y, std::string text);

// Complete control row generated from a registered Setting. Returns the row height.
struct SettingRowOptions {
    float labelWidth = 0.f;      // 0 = auto
    std::function<void(Setting&)> onEdited;
};
float settingRow(Ui& ui, Setting& s, float x, float y, float w, const SettingRowOptions& opt = {});

// ── Scrolling ────────────────────────────────────────────────────────────────────────────────
struct ScrollState {
    Spring pos;
    float target = 0.f;
    float content = 0.f;
    float view = 0.f;
};
ScrollState& scrollState(Ui& ui, Id id);
// Begins a clipped, vertically scrolling region. Returns the y-offset to add to content.
float beginScroll(Ui& ui, Id id, const Rect& view);
void endScroll(Ui& ui, Id id, const Rect& view, float contentHeight);
void scrollTo(Ui& ui, Id id, float y);

}  // namespace vigil::ui
