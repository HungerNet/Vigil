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
    // While a modal overlay (search, onboarding) is open, widgets underneath ignore input.
    bool modal = false;

    // Deep-link flash: the setting row with this key pulses and reports its position.
    std::string flashKey;
    double flashAt = -10.0;
    float flashY = -1.f;    // screen y of the flashed row in the last frame (−1 = not drawn)

    // ── Keyboard focus (PLAN.md §8) ──────────────────────────────────────────────────────────
    // Every pressable control registers itself in drawing (reading) order. Tab / Shift+Tab move
    // `kbFocus` through that order; Enter or Space activate the focused control; arrows adjust
    // sliders, segmented controls, tabs and dropdowns. The focus ring is drawn after keyboard
    // use (or always, by setting) and hidden again by a mouse click.
    Id kbFocus = 0;
    bool focusVisible = false;
    bool focusRingAlways = false;
    Id keyActivated = 0;      // control activated with Enter/Space this frame
    // Numeric entry: the value box being edited (sliders' typed values), its text, and when an
    // invalid entry last shook it.
    Id editingNumber = 0;
    std::string editText;
    double editShakeAt = -10.0;

    const Theme& th() const { return theme(); }

    void beginFrame(double nowSeconds);
    void endFrame();

    bool hover(const Rect& rc) const;
    bool hoverRaw(const Rect& rc) const { return rc.contains(in.mx - r.offsetX(), in.my - r.offsetY()); }
    // Standard press/release behavior. Returns true on click, or on Enter/Space while focused.
    bool pressable(Id id, const Rect& rc, bool* hovered = nullptr, bool* held = nullptr);
    bool consumeKey(int vk, bool* ctrl = nullptr, bool* shift = nullptr);
    // Keyboard focus helpers.
    bool focused(Id id) const { return kbFocus == id; }
    bool keyboardFocused(Id id) const { return kbFocus == id && (focusVisible || focusRingAlways); }
    void setKeyboardFocus(Id id) { kbFocus = id; focusVisible = true; scrollFocusIntoView_ = true; }
    // Moves focus to the next (+1) or previous (−1) control drawn this frame. Returns false when
    // nothing is focusable.
    bool moveFocus(int direction);
    std::size_t focusableCount() const { return focusables_.size(); }
    // Scroll regions report themselves so a newly focused control can be scrolled into view.
    void pushScrollRegion(Id id) { scrollStack_.push_back(id); }
    void popScrollRegion() { if (!scrollStack_.empty()) scrollStack_.pop_back(); }

    Spring& spring(Id id);
    // Layout moved under every spring (interface scale or window size changed): springs jump to
    // their next target instead of gliding across the screen from stale positions.
    void snapSprings();
    float& value(Id id, float init = 0.f);
    float anim(Id id, float target, float rate = 14.f);   // exponential approach helper
    float springTo(Id id, float target, float stiffness = 260.f, float damping = 26.f);

    void tooltip(Id id, const Rect& anchor, std::string text);
    void drawTooltip();
    void addOverlay(std::function<void()> fn) { overlays.push_back(std::move(fn)); }

private:
    struct Tip { Id id = 0; std::string text; Rect anchor; double since = 0; float alpha = 0.f; bool requested = false; };
    struct Focusable { Id id; Rect rc; };   // rc in screen space
    void drawFocusRing();
    Tip tip_;
    std::unordered_map<Id, Spring> springs_;
    std::unordered_map<Id, float> values_;
    std::vector<Focusable> focusables_;
    std::vector<Id> scrollStack_;
    Rect focusRect_;
    bool focusDrawn_ = false;
    int focusMissingFrames_ = 0;
    bool scrollFocusIntoView_ = false;
    float ringAlpha_ = 0.f;
};

// ── Pure helpers (unit-tested in tests/ui_logic_tests.cpp) ────────────────────────────────────
// Parses a typed number ("1.25", "1,25", "0.40 b/tick", "75%", "1.2x"), then clamps it to
// [min, max] and snaps it to `step` (0 = continuous). False when the text holds no number.
bool parseNumberInput(std::string_view text, double min, double max, double step, double& out);
// Index of the control that receives focus when moving `direction` from `current` through
// `count` controls (wraps; −1 current = start from the edge). −1 when count is 0.
int nextFocusIndex(int current, int count, int direction);

// ── Drawing helpers ──────────────────────────────────────────────────────────────────────────
void card(Ui& ui, const Rect& rc, bool raised = false, float radius = -1.f);
void divider(Ui& ui, float x0, float x1, float y);
void sectionHeader(Ui& ui, float x, float y, std::string_view title, std::string_view subtitle = {});
void chip(Ui& ui, const Rect& rc, std::string_view text, Color c, bool filled = false);
float chipWidth(Ui& ui, std::string_view text);
void keycap(Ui& ui, const Rect& rc, std::string_view text);   // keyboard key label
float keycapWidth(Ui& ui, std::string_view text);
void statusDot(Ui& ui, float cx, float cy, Color c, bool pulse);
void confidenceRing(Ui& ui, Id id, float cx, float cy, float radius, float value, float stroke = 4.f, bool label = true);
void meter(Ui& ui, const Rect& rc, float fraction, Color c);
void emptyState(Ui& ui, const Rect& rc, Icon icon, std::string_view title, std::string_view body);
std::string formatNumber(double v, int decimals);
std::string formatSettingValue(const Setting& s, double v);
std::string formatDuration(double seconds);
std::string formatClock(double seconds);   // m:ss or h:mm:ss
std::string keyName(int vk);
void copyToClipboard(const std::string& text);

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
// Underlined tab strip; returns true when the selection changed.
bool tabs(Ui& ui, Id id, float x, float y, const std::vector<std::string>& labels, int& selected, float* outWidth = nullptr);
bool dropdown(Ui& ui, Id id, const Rect& rc, const std::vector<std::string>& options, int& selected);
bool textField(Ui& ui, Id id, const Rect& rc, std::string& text, std::string_view placeholder, Icon leading = Icon::Search,
               bool masked = false, std::size_t maxLength = 64);
bool keybind(Ui& ui, Id id, const Rect& rc, int& vk);
void infoIcon(Ui& ui, Id id, float x, float y, std::string text);
// The value shown next to a slider. Click it (or press Enter on the focused slider `sliderId`)
// to type a value: Enter commits (clamped, snapped), Esc cancels, an invalid entry shakes.
// Returns true when `value` changed.
bool numberBox(Ui& ui, Id id, Id sliderId, const Rect& rc, double& value, double min, double max, double step,
               const std::string& display, bool modified);

// Key-binding conflicts: other Vigil bindings and Minecraft's default controls.
std::string keyConflict(const Setting& keySetting, Setting** otherVigilBinding = nullptr);

// Complete control row generated from a registered Setting. Returns the row height.
struct SettingRowOptions {
    std::function<void(Setting&)> onEdited;
};
float settingRow(Ui& ui, Setting& s, float x, float y, float w, const SettingRowOptions& opt = {});
float settingRowHeight(const Setting& s);

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
// Scrolls `id` so a flashed setting row drawn this frame is visible (call after drawing rows).
void scrollToFlash(Ui& ui, Id id, const Rect& view, float contentTop);

}  // namespace vigil::ui
