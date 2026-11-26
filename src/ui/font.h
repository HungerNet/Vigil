#pragma once
// Vigil UI — typography. Embedded Inter (SIL OFL) rasterized through GDI glyph outlines into
// grayscale atlases at exact device-pixel sizes, so text stays crisp at every UI scale.

#include "gfx.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vigil::ui {

enum class Face { Regular, Medium, SemiBold, Display, Mono };

struct TextStyle {
    Face face = Face::Regular;
    float size = 13.f;       // dp
    float tracking = 0.f;    // extra spacing between glyphs, dp (applied in whole device pixels)
    bool tabular = false;    // digits on a fixed advance, so live numbers don't jitter
};

// Type scale. Adjacent levels differ by one clear step; tracking tightens as size grows.
namespace type {
inline constexpr TextStyle Overline{Face::SemiBold, 10.5f, 0.8f};
inline constexpr TextStyle Caption{Face::Medium, 11.f, 0.f};
inline constexpr TextStyle Small{Face::Regular, 12.f, 0.f};
inline constexpr TextStyle SmallStrong{Face::Medium, 12.f, 0.f};
inline constexpr TextStyle Body{Face::Regular, 13.f, 0.f};
inline constexpr TextStyle BodyStrong{Face::Medium, 13.f, 0.f};
inline constexpr TextStyle Label{Face::Medium, 14.f, 0.f};
inline constexpr TextStyle Title{Face::SemiBold, 16.f, 0.f};
inline constexpr TextStyle Heading{Face::Display, 24.f, -0.3f};
inline constexpr TextStyle Hero{Face::Display, 32.f, -0.6f};
inline constexpr TextStyle Metric{Face::Display, 28.f, -0.4f, true};
inline constexpr TextStyle Mono{Face::Mono, 12.f, 0.f};
inline constexpr TextStyle Wordmark{Face::SemiBold, 15.f, 3.f};
// Numbers that change while you look at them (confidence, counts, timers).
inline constexpr TextStyle Numeric{Face::Medium, 12.f, 0.f, true};
inline constexpr TextStyle NumericLarge{Face::SemiBold, 16.f, 0.f, true};
inline constexpr TextStyle NumericCaption{Face::Medium, 11.f, 0.f, true};
}  // namespace type

enum class Align { Left, Center, Right };

class Fonts {
public:
    Fonts();
    ~Fonts();
    bool initialize();     // registers embedded faces; safe to call repeatedly
    void releaseGraphics();
    void setScale(float scale);
    // Frees atlases that have not been used for a while (call once per frame with GL current).
    // Scale changes and window resizes create atlases at new pixel sizes; this keeps them bounded.
    void collect();

    float width(const TextStyle& s, std::string_view text);
    float lineHeight(const TextStyle& s);   // dp
    float ascent(const TextStyle& s);       // dp from the top of the line box to the baseline
    // Draws with the top of the line box at y. Returns advance width (dp).
    float draw(Renderer& r, const TextStyle& s, float x, float y, std::string_view text, Color c,
               Align align = Align::Left, float maxWidth = 0.f);
    // Word-wraps into lines no wider than `width`.
    std::vector<std::string> wrap(const TextStyle& s, std::string_view text, float width);
    float drawWrapped(Renderer& r, const TextStyle& s, float x, float y, float width, std::string_view text, Color c,
                      float lineGap = 3.f, int maxLines = 0);
    std::string ellipsize(const TextStyle& s, std::string_view text, float maxWidth);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Decodes the next UTF-8 code point; advances `i`.
unsigned int nextCodepoint(std::string_view s, std::size_t& i);

}  // namespace vigil::ui
