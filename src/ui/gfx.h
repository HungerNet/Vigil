#pragma once
// Vigil UI — immediate-mode 2D renderer on fixed-function OpenGL.
//
// Coordinates are in density-independent units ("dp"); the renderer converts to device pixels
// with a global scale so every edge can be anti-aliased with an exact 1px feather. The renderer
// saves and restores all GL state it touches because it draws inside another application's frame.

#include <cstdint>
#include <vector>

namespace vigil::ui {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    constexpr Color() = default;
    constexpr Color(float rr, float gg, float bb, float aa = 1.f) : r(rr), g(gg), b(bb), a(aa) {}
    static constexpr Color hex(std::uint32_t rgb, float alpha = 1.f) {
        return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, alpha};
    }
    Color alpha(float a2) const { return {r, g, b, a * a2}; }
    Color withAlpha(float a2) const { return {r, g, b, a2}; }
    Color mix(const Color& o, float t) const { return {r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, a + (o.a - a) * t}; }
    Color lighter(float t) const { return mix({1, 1, 1, a}, t); }
    Color darker(float t) const { return mix({0, 0, 0, a}, t); }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float r() const { return x + w; }
    float b() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect inset(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    Rect offset(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
    Rect intersect(const Rect& o) const;
    // Slicing helpers for layout.
    Rect takeTop(float hh) { Rect t{x, y, w, hh}; y += hh; h -= hh; return t; }
    Rect takeBottom(float hh) { Rect t{x, y + h - hh, w, hh}; h -= hh; return t; }
    Rect takeLeft(float ww) { Rect t{x, y, ww, h}; x += ww; w -= ww; return t; }
    Rect takeRight(float ww) { Rect t{x + w - ww, y, ww, h}; w -= ww; return t; }
};

struct Point {
    float x, y;
};

class Renderer {
public:
    // `forceDefaultFramebuffer` binds framebuffer 0 for drawing (in-game); the preview renders into its own FBO.
    void begin(int pixelWidth, int pixelHeight, float scale, bool forceDefaultFramebuffer);
    void end();
    void releaseResources();

    float scale() const { return scale_; }
    float width() const { return pixelW_ / scale_; }   // dp
    float height() const { return pixelH_ / scale_; }
    int pixelWidth() const { return pixelW_; }
    int pixelHeight() const { return pixelH_; }
    float px() const { return 1.f / scale_; }          // one device pixel in dp

    // State stacks.
    void pushClip(const Rect& r);
    void popClip();
    Rect clip() const { return clips_.empty() ? Rect{0, 0, width(), height()} : clips_.back(); }
    void pushAlpha(float a);
    void popAlpha();
    void pushOffset(float dx, float dy);
    void popOffset();
    float offsetX() const { return ox_; }
    float offsetY() const { return oy_; }

    // Shapes (all anti-aliased).
    void rect(const Rect& r, Color c);
    void roundRect(const Rect& r, float radius, Color c);
    void roundRectGradient(const Rect& r, float radius, Color top, Color bottom);
    void roundRectHGradient(const Rect& r, float radius, Color left, Color right);
    void roundRectStroke(const Rect& r, float radius, float width, Color c);
    void shadow(const Rect& r, float radius, float blur, Color c, float offsetY = 0.f, float spread = 0.f);
    void circle(float cx, float cy, float radius, Color c);
    void ring(float cx, float cy, float radius, float width, Color c);
    // Arc from a0 to a1 (radians, 0 = up, clockwise).
    void arc(float cx, float cy, float radius, float width, float a0, float a1, Color c, bool roundCaps = true);
    void line(float x0, float y0, float x1, float y1, float width, Color c, bool roundCaps = true);
    void polyline(const std::vector<Point>& pts, float width, Color c, bool closed = false);
    void polygon(const std::vector<Point>& pts, Color c);   // simple polygon, any winding
    void areaUnder(const std::vector<Point>& pts, float baseline, Color top, Color bottom);
    void diamond(float cx, float cy, float radius, Color c);

    // RGBA texture region drawn into `r` (dp). Texture parameters are never modified, so
    // textures owned by the host (e.g. Minecraft skins) are drawn exactly as configured.
    void image(unsigned int texture, const Rect& r, float u0, float v0, float u1, float v1, Color tint = {1, 1, 1, 1});
    // The texture region mapped onto a disc (circular player faces). The rim is not feathered:
    // draw a ring over it (playerHalo does) for an anti-aliased edge.
    void imageDisc(unsigned int texture, float cx, float cy, float radius, float u0, float v0, float u1, float v1,
                   Color tint = {1, 1, 1, 1});

    // Textured quad in dp with a GL_ALPHA texture (used by fonts).
    void beginGlyphs(unsigned int texture, Color c);
    void glyph(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1);
    void endGlyphs();

    // Frosted backdrop: blurs what is already on screen, then tints it.
    void backdrop(float amount, float blurStrength, Color tint);

private:
    void vertex(float x, float y) const;
    void color(const Color& c) const;
    void contour(const Rect& r, float radius, float delta, std::vector<Point>& out) const;
    void fillContourAA(const std::vector<Point>& inner, const std::vector<Point>& outer, const Rect& r,
                       Color top, Color bottom, bool horizontal) const;
    void applyClip() const;
    void ensureBlurTargets();

    int pixelW_ = 0, pixelH_ = 0;
    float scale_ = 1.f;
    float ox_ = 0, oy_ = 0;
    std::vector<Rect> clips_;
    std::vector<float> alphas_;
    std::vector<Point> offsets_;
    float alpha_ = 1.f;
    bool active_ = false;

    // Saved GL state not covered by glPushAttrib.
    int savedProgram_ = 0, savedDrawFbo_ = 0, savedReadFbo_ = 0, savedActiveTex_ = 0, savedUnpackPbo_ = 0, savedArrayBuf_ = 0;

    unsigned int blurTex_[3] = {0, 0, 0};
    int blurW_ = 0, blurH_ = 0;
};

// GL extension entry points (resolved lazily through wglGetProcAddress).
namespace glx {
bool load();
void useProgram(int p);
void bindFramebuffer(unsigned int target, unsigned int fbo);
void activeTexture(unsigned int unit);
void bindBuffer(unsigned int target, unsigned int buf);
bool hasFramebuffers();
// RGBA8 texture with nearest filtering (pixel art such as generated player faces).
unsigned int createTextureRGBA(int w, int h, const unsigned char* pixels);
// Preview harness helpers.
unsigned int createFramebuffer(int w, int h, unsigned int& colorTex);
void deleteFramebuffer(unsigned int fbo, unsigned int colorTex);
}  // namespace glx

}  // namespace vigil::ui
