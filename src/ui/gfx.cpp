#include "gfx.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cmath>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#define VGL_CURRENT_PROGRAM 0x8B8D
#define VGL_FRAMEBUFFER 0x8D40
#define VGL_READ_FRAMEBUFFER 0x8CA8
#define VGL_DRAW_FRAMEBUFFER 0x8CA9
#define VGL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define VGL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define VGL_ACTIVE_TEXTURE 0x84E0
#define VGL_TEXTURE0 0x84C0
#define VGL_PIXEL_UNPACK_BUFFER 0x88EC
#define VGL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#define VGL_ARRAY_BUFFER 0x8892
#define VGL_ARRAY_BUFFER_BINDING 0x8894
#define VGL_COLOR_ATTACHMENT0 0x8CE0

namespace vigil::ui {

Rect Rect::intersect(const Rect& o) const {
    const float nx = std::max(x, o.x), ny = std::max(y, o.y);
    const float nr = std::min(r(), o.r()), nb = std::min(b(), o.b());
    return {nx, ny, std::max(0.f, nr - nx), std::max(0.f, nb - ny)};
}

// ── GL extensions ────────────────────────────────────────────────────────────────────────────
namespace glx {
namespace {
using PFNUseProgram = void(APIENTRY*)(GLuint);
using PFNBindFramebuffer = void(APIENTRY*)(GLenum, GLuint);
using PFNActiveTexture = void(APIENTRY*)(GLenum);
using PFNBindBuffer = void(APIENTRY*)(GLenum, GLuint);
using PFNGenFramebuffers = void(APIENTRY*)(GLsizei, GLuint*);
using PFNDeleteFramebuffers = void(APIENTRY*)(GLsizei, const GLuint*);
using PFNFramebufferTexture2D = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
PFNUseProgram pUseProgram = nullptr;
PFNBindFramebuffer pBindFramebuffer = nullptr;
PFNActiveTexture pActiveTexture = nullptr;
PFNBindBuffer pBindBuffer = nullptr;
PFNGenFramebuffers pGenFramebuffers = nullptr;
PFNDeleteFramebuffers pDeleteFramebuffers = nullptr;
PFNFramebufferTexture2D pFramebufferTexture2D = nullptr;
bool loaded = false;

template <typename T>
T proc(const char* a, const char* b = nullptr) {
    PROC p = wglGetProcAddress(a);
    if (!p && b) p = wglGetProcAddress(b);
    return reinterpret_cast<T>(p);
}
}  // namespace

bool load() {
    if (loaded) return true;
    if (!wglGetCurrentContext()) return false;
    pUseProgram = proc<PFNUseProgram>("glUseProgram", "glUseProgramObjectARB");
    pBindFramebuffer = proc<PFNBindFramebuffer>("glBindFramebuffer", "glBindFramebufferEXT");
    pActiveTexture = proc<PFNActiveTexture>("glActiveTexture", "glActiveTextureARB");
    pBindBuffer = proc<PFNBindBuffer>("glBindBuffer", "glBindBufferARB");
    pGenFramebuffers = proc<PFNGenFramebuffers>("glGenFramebuffers", "glGenFramebuffersEXT");
    pDeleteFramebuffers = proc<PFNDeleteFramebuffers>("glDeleteFramebuffers", "glDeleteFramebuffersEXT");
    pFramebufferTexture2D = proc<PFNFramebufferTexture2D>("glFramebufferTexture2D", "glFramebufferTexture2DEXT");
    loaded = true;
    return true;
}
void useProgram(int p) { if (pUseProgram) pUseProgram(static_cast<GLuint>(p)); }
void bindFramebuffer(unsigned int target, unsigned int fbo) { if (pBindFramebuffer) pBindFramebuffer(target, fbo); }
void activeTexture(unsigned int unit) { if (pActiveTexture) pActiveTexture(unit); }
void bindBuffer(unsigned int target, unsigned int buf) { if (pBindBuffer) pBindBuffer(target, buf); }
bool hasFramebuffers() { return pBindFramebuffer && pGenFramebuffers && pFramebufferTexture2D; }

unsigned int createTextureRGBA(int w, int h, const unsigned char* pixels) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return tex;
}

unsigned int createFramebuffer(int w, int h, unsigned int& colorTex) {
    load();
    if (!hasFramebuffers()) return 0;
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    GLuint fbo = 0;
    pGenFramebuffers(1, &fbo);
    pBindFramebuffer(VGL_FRAMEBUFFER, fbo);
    pFramebufferTexture2D(VGL_FRAMEBUFFER, VGL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
    return fbo;
}

void deleteFramebuffer(unsigned int fbo, unsigned int colorTex) {
    if (pDeleteFramebuffers && fbo) pDeleteFramebuffers(1, &fbo);
    if (colorTex) glDeleteTextures(1, &colorTex);
}
}  // namespace glx

// ── Frame lifecycle ──────────────────────────────────────────────────────────────────────────

void Renderer::begin(int pixelWidth, int pixelHeight, float scale, bool forceDefaultFramebuffer) {
    glx::load();
    pixelW_ = std::max(1, pixelWidth);
    pixelH_ = std::max(1, pixelHeight);
    scale_ = std::max(0.25f, scale);
    clips_.clear();
    alphas_.clear();
    offsets_.clear();
    alpha_ = 1.f;
    ox_ = oy_ = 0.f;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glGetIntegerv(VGL_CURRENT_PROGRAM, &savedProgram_);
    glGetIntegerv(VGL_DRAW_FRAMEBUFFER_BINDING, &savedDrawFbo_);
    glGetIntegerv(VGL_READ_FRAMEBUFFER_BINDING, &savedReadFbo_);
    glGetIntegerv(VGL_ACTIVE_TEXTURE, &savedActiveTex_);
    glGetIntegerv(VGL_PIXEL_UNPACK_BUFFER_BINDING, &savedUnpackPbo_);
    glGetIntegerv(VGL_ARRAY_BUFFER_BINDING, &savedArrayBuf_);
    while (glGetError() != GL_NO_ERROR) {}  // queries unsupported on old contexts raise errors

    glx::useProgram(0);
    if (forceDefaultFramebuffer) glx::bindFramebuffer(VGL_FRAMEBUFFER, 0);
    glx::bindBuffer(VGL_PIXEL_UNPACK_BUFFER, 0);
    glx::bindBuffer(VGL_ARRAY_BUFFER, 0);
    for (int unit = 3; unit >= 0; --unit) {
        glx::activeTexture(VGL_TEXTURE0 + unit);
        glDisable(GL_TEXTURE_2D);
        glMatrixMode(GL_TEXTURE);
        glPushMatrix();
        glLoadIdentity();
    }
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, pixelW_, pixelH_, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glViewport(0, 0, pixelW_, pixelH_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_POLYGON_SMOOTH);
    glDisable(GL_COLOR_MATERIAL);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glShadeModel(GL_SMOOTH);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    active_ = true;
}

void Renderer::end() {
    if (!active_) return;
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    for (int unit = 3; unit >= 0; --unit) {
        glx::activeTexture(VGL_TEXTURE0 + unit);
        glMatrixMode(GL_TEXTURE);
        glPopMatrix();
    }
    glMatrixMode(GL_MODELVIEW);
    glx::activeTexture(static_cast<unsigned int>(savedActiveTex_ ? savedActiveTex_ : VGL_TEXTURE0));
    glx::bindBuffer(VGL_ARRAY_BUFFER, static_cast<unsigned int>(savedArrayBuf_));
    glx::bindBuffer(VGL_PIXEL_UNPACK_BUFFER, static_cast<unsigned int>(savedUnpackPbo_));
    glx::bindFramebuffer(VGL_DRAW_FRAMEBUFFER, static_cast<unsigned int>(savedDrawFbo_));
    glx::bindFramebuffer(VGL_READ_FRAMEBUFFER, static_cast<unsigned int>(savedReadFbo_));
    glx::useProgram(savedProgram_);
    glPopClientAttrib();
    glPopAttrib();
    while (glGetError() != GL_NO_ERROR) {}
    active_ = false;
}

void Renderer::releaseResources() {
    for (unsigned int& t : blurTex_) {
        if (t) glDeleteTextures(1, &t);
        t = 0;
    }
    blurW_ = blurH_ = 0;
}

// ── State stacks ─────────────────────────────────────────────────────────────────────────────

void Renderer::applyClip() const {
    if (clips_.empty()) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    const Rect& c = clips_.back();
    glEnable(GL_SCISSOR_TEST);
    const int x0 = static_cast<int>(std::floor(c.x * scale_));
    const int y0 = static_cast<int>(std::floor(c.y * scale_));
    const int x1 = static_cast<int>(std::ceil(c.r() * scale_));
    const int y1 = static_cast<int>(std::ceil(c.b() * scale_));
    glScissor(x0, pixelH_ - y1, std::max(0, x1 - x0), std::max(0, y1 - y0));
}

void Renderer::pushClip(const Rect& r) {
    Rect abs = r.offset(ox_, oy_);
    if (!clips_.empty()) abs = abs.intersect(clips_.back());
    clips_.push_back(abs);
    applyClip();
}

void Renderer::popClip() {
    if (!clips_.empty()) clips_.pop_back();
    applyClip();
}

void Renderer::pushAlpha(float a) {
    alphas_.push_back(alpha_);
    alpha_ *= std::clamp(a, 0.f, 1.f);
}

void Renderer::popAlpha() {
    if (alphas_.empty()) return;
    alpha_ = alphas_.back();
    alphas_.pop_back();
}

void Renderer::pushOffset(float dx, float dy) {
    offsets_.push_back({ox_, oy_});
    ox_ += dx;
    oy_ += dy;
}

void Renderer::popOffset() {
    if (offsets_.empty()) return;
    ox_ = offsets_.back().x;
    oy_ = offsets_.back().y;
    offsets_.pop_back();
}

// ── Primitives ───────────────────────────────────────────────────────────────────────────────

void Renderer::vertex(float x, float y) const { glVertex2f((x + ox_) * scale_, (y + oy_) * scale_); }

void Renderer::color(const Color& c) const { glColor4f(c.r, c.g, c.b, c.a * alpha_); }

void Renderer::contour(const Rect& r, float radius, float delta, std::vector<Point>& out) const {
    out.clear();
    const float rad = std::max(0.f, std::min(radius, std::min(r.w, r.h) * 0.5f));
    const int seg = std::clamp(static_cast<int>(rad * scale_ * 0.45f) + 3, 3, 24);
    const float rr = std::max(0.f, rad + delta);
    const float x0 = r.x + rad, x1 = r.r() - rad, y0 = r.y + rad, y1 = r.b() - rad;
    const float centers[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    const float starts[4] = {3.14159265f, 4.71238898f, 0.f, 1.57079633f};
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i <= seg; ++i) {
            const float a = starts[c] + 1.57079633f * static_cast<float>(i) / seg;
            out.push_back({centers[c][0] + std::cos(a) * rr, centers[c][1] + std::sin(a) * rr});
        }
    }
}

void Renderer::fillContourAA(const std::vector<Point>& inner, const std::vector<Point>& outer, const Rect& r,
                             Color top, Color bottom, bool horizontal) const {
    auto colorAt = [&](const Point& p, float a) {
        float t = horizontal ? (p.x - r.x) / std::max(1e-3f, r.w) : (p.y - r.y) / std::max(1e-3f, r.h);
        t = std::clamp(t, 0.f, 1.f);
        Color c = top.mix(bottom, t);
        c.a *= a;
        color(c);
    };
    glBegin(GL_TRIANGLE_FAN);
    colorAt({r.cx(), r.cy()}, 1.f);
    vertex(r.cx(), r.cy());
    for (const Point& p : inner) { colorAt(p, 1.f); vertex(p.x, p.y); }
    colorAt(inner.front(), 1.f);
    vertex(inner.front().x, inner.front().y);
    glEnd();
    glBegin(GL_TRIANGLE_STRIP);
    for (std::size_t i = 0; i <= inner.size(); ++i) {
        const Point& a = inner[i % inner.size()];
        const Point& b = outer[i % outer.size()];
        colorAt(a, 1.f);
        vertex(a.x, a.y);
        colorAt(b, 0.f);
        vertex(b.x, b.y);
    }
    glEnd();
}

void Renderer::rect(const Rect& r, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    color(c);
    glBegin(GL_QUADS);
    vertex(r.x, r.y);
    vertex(r.r(), r.y);
    vertex(r.r(), r.b());
    vertex(r.x, r.b());
    glEnd();
}

void Renderer::roundRect(const Rect& r, float radius, Color c) { roundRectGradient(r, radius, c, c); }

void Renderer::roundRectGradient(const Rect& r, float radius, Color top, Color bottom) {
    if (r.w <= 0 || r.h <= 0) return;
    std::vector<Point> inner, outer;
    const float f = 0.5f / scale_;
    contour(r, radius, -f, inner);
    contour(r, radius, f, outer);
    fillContourAA(inner, outer, r, top, bottom, false);
}

void Renderer::roundRectHGradient(const Rect& r, float radius, Color left, Color right) {
    if (r.w <= 0 || r.h <= 0) return;
    std::vector<Point> inner, outer;
    const float f = 0.5f / scale_;
    contour(r, radius, -f, inner);
    contour(r, radius, f, outer);
    fillContourAA(inner, outer, r, left, right, true);
}

void Renderer::roundRectStroke(const Rect& r, float radius, float width, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    float wpx = width * scale_;
    if (wpx < 1.f) { c.a *= wpx; wpx = 1.f; }
    const float f = 0.5f / scale_;
    const float w = wpx / scale_;
    std::vector<Point> c0, c1, c2, c3;
    contour(r, radius, -w - f, c0);
    contour(r, radius, -w + f, c1);
    contour(r, radius, -f, c2);
    contour(r, radius, f, c3);
    auto strip = [&](const std::vector<Point>& a, float aa, const std::vector<Point>& b, float ab) {
        glBegin(GL_TRIANGLE_STRIP);
        for (std::size_t i = 0; i <= a.size(); ++i) {
            color(c.alpha(aa));
            vertex(a[i % a.size()].x, a[i % a.size()].y);
            color(c.alpha(ab));
            vertex(b[i % b.size()].x, b[i % b.size()].y);
        }
        glEnd();
    };
    strip(c0, 0.f, c1, 1.f);
    strip(c1, 1.f, c2, 1.f);
    strip(c2, 1.f, c3, 0.f);
}

void Renderer::shadow(const Rect& r0, float radius, float blur, Color c, float offsetY, float spread) {
    Rect r{r0.x - spread, r0.y - spread + offsetY, r0.w + 2 * spread, r0.h + 2 * spread};
    if (blur <= 0.f) { roundRect(r, radius, c); return; }
    constexpr int kSteps = 7;
    std::vector<std::vector<Point>> rings(kSteps + 1);
    float alphas[kSteps + 1];
    for (int i = 0; i <= kSteps; ++i) {
        const float t = static_cast<float>(i) / kSteps;
        contour(r, radius + blur * 0.5f, -blur * 0.5f + t * blur, rings[static_cast<std::size_t>(i)]);
        const float s = t * t * (3.f - 2.f * t);
        alphas[i] = 1.f - s;
    }
    // Core.
    {
        const auto& in = rings[0];
        Rect core = r.inset(blur * 0.5f);
        glBegin(GL_TRIANGLE_FAN);
        color(c);
        vertex(core.cx(), core.cy());
        for (const Point& p : in) vertex(p.x, p.y);
        vertex(in.front().x, in.front().y);
        glEnd();
    }
    for (int i = 0; i < kSteps; ++i) {
        const auto& a = rings[static_cast<std::size_t>(i)];
        const auto& b = rings[static_cast<std::size_t>(i + 1)];
        glBegin(GL_TRIANGLE_STRIP);
        for (std::size_t k = 0; k <= a.size(); ++k) {
            color(c.alpha(alphas[i]));
            vertex(a[k % a.size()].x, a[k % a.size()].y);
            color(c.alpha(alphas[i + 1]));
            vertex(b[k % b.size()].x, b[k % b.size()].y);
        }
        glEnd();
    }
}

void Renderer::circle(float cx, float cy, float radius, Color c) {
    if (radius <= 0.f) return;
    const int seg = std::clamp(static_cast<int>(radius * scale_ * 0.9f) + 12, 12, 128);
    const float f = 0.5f / scale_;
    const float ri = std::max(0.f, radius - f), ro = radius + f;
    glBegin(GL_TRIANGLE_FAN);
    color(c);
    vertex(cx, cy);
    for (int i = 0; i <= seg; ++i) {
        const float a = 6.2831853f * i / seg;
        vertex(cx + std::cos(a) * ri, cy + std::sin(a) * ri);
    }
    glEnd();
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= seg; ++i) {
        const float a = 6.2831853f * i / seg;
        const float ca = std::cos(a), sa = std::sin(a);
        color(c);
        vertex(cx + ca * ri, cy + sa * ri);
        color(c.alpha(0.f));
        vertex(cx + ca * ro, cy + sa * ro);
    }
    glEnd();
}

void Renderer::ring(float cx, float cy, float radius, float width, Color c) {
    arc(cx, cy, radius, width, 0.f, 6.2831853f, c, false);
}

void Renderer::arc(float cx, float cy, float radius, float width, float a0, float a1, Color c, bool roundCaps) {
    if (a1 < a0) std::swap(a0, a1);
    const float span = a1 - a0;
    if (span <= 1e-4f || width <= 0.f) return;
    const int seg = std::clamp(static_cast<int>(radius * scale_ * span * 0.35f) + 6, 6, 256);
    const float f = 0.5f / scale_;
    const float hw = width * 0.5f;
    const float r0 = radius - hw - f, r1 = radius - hw + f, r2 = radius + hw - f, r3 = radius + hw + f;
    auto pt = [&](float rr, float a) { return Point{cx + std::sin(a) * rr, cy - std::cos(a) * rr}; };
    auto strip = [&](float ra, float aa, float rb, float ab) {
        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 0; i <= seg; ++i) {
            const float a = a0 + span * i / seg;
            const Point p = pt(ra, a), q = pt(rb, a);
            color(c.alpha(aa));
            vertex(p.x, p.y);
            color(c.alpha(ab));
            vertex(q.x, q.y);
        }
        glEnd();
    };
    strip(std::max(0.f, r0), 0.f, std::max(0.f, r1), 1.f);
    if (r2 > r1) strip(r1, 1.f, r2, 1.f);
    strip(r2, 1.f, r3, 0.f);
    if (roundCaps && span < 6.28f) {
        const Point s = pt(radius, a0), e = pt(radius, a1);
        circle(s.x, s.y, hw, c);
        circle(e.x, e.y, hw, c);
    }
}

void Renderer::line(float x0, float y0, float x1, float y1, float width, Color c, bool roundCaps) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-5f) return;
    float wpx = width * scale_;
    if (wpx < 1.f) { c.a *= wpx; wpx = 1.f; }
    const float hw = wpx * 0.5f / scale_;
    const float f = 0.5f / scale_;
    const float ux = dx / len, uy = dy / len;
    const float nx = -uy, ny = ux;
    const float o = hw + f, in = std::max(0.f, hw - f);
    const float ex = roundCaps ? 0.f : f;
    const float ax = x0 - ux * ex, ay = y0 - uy * ex, bx = x1 + ux * ex, by = y1 + uy * ex;
    glBegin(GL_TRIANGLE_STRIP);
    color(c.alpha(0.f));
    vertex(ax - nx * o, ay - ny * o);
    vertex(bx - nx * o, by - ny * o);
    color(c);
    vertex(ax - nx * in, ay - ny * in);
    vertex(bx - nx * in, by - ny * in);
    vertex(ax + nx * in, ay + ny * in);
    vertex(bx + nx * in, by + ny * in);
    color(c.alpha(0.f));
    vertex(ax + nx * o, ay + ny * o);
    vertex(bx + nx * o, by + ny * o);
    glEnd();
    if (roundCaps) {
        circle(x0, y0, hw, c);
        circle(x1, y1, hw, c);
    }
}

void Renderer::polyline(const std::vector<Point>& pts, float width, Color c, bool closed) {
    if (pts.size() < 2) return;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) line(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, width, c, true);
    if (closed) line(pts.back().x, pts.back().y, pts.front().x, pts.front().y, width, c, true);
}

namespace {

float cross(const Point& o, const Point& a, const Point& b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

bool insideTriangle(const Point& p, const Point& a, const Point& b, const Point& c) {
    const float d1 = cross(p, a, b), d2 = cross(p, b, c), d3 = cross(p, c, a);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

}  // namespace

void Renderer::polygon(const std::vector<Point>& pts, Color c) {
    const std::size_t n = pts.size();
    if (n < 3) return;
    float area = 0.f;
    for (std::size_t i = 0; i < n; ++i) {
        const Point& a = pts[i];
        const Point& b = pts[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    const bool ccw = area > 0.f;  // in y-down screen space "ccw" here means clockwise visually
    // Ear clipping.
    std::vector<std::size_t> idx(n);
    for (std::size_t i = 0; i < n; ++i) idx[i] = i;
    color(c);
    glBegin(GL_TRIANGLES);
    int guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool clipped = false;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            const Point& a = pts[idx[(i + idx.size() - 1) % idx.size()]];
            const Point& b = pts[idx[i]];
            const Point& d = pts[idx[(i + 1) % idx.size()]];
            const float cr = cross(a, b, d);
            if (ccw ? cr <= 0.f : cr >= 0.f) continue;  // reflex
            bool ear = true;
            for (std::size_t j : idx) {
                const Point& p = pts[j];
                if (&p == &a || &p == &b || &p == &d) continue;
                if (insideTriangle(p, a, b, d)) { ear = false; break; }
            }
            if (!ear) continue;
            vertex(a.x, a.y);
            vertex(b.x, b.y);
            vertex(d.x, d.y);
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) {
        for (std::size_t k : idx) vertex(pts[k].x, pts[k].y);
    }
    glEnd();

    // Feathered outline using per-vertex miter normals.
    const float f = 0.75f / scale_;
    std::vector<Point> outer(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Point& p0 = pts[(i + n - 1) % n];
        const Point& p1 = pts[i];
        const Point& p2 = pts[(i + 1) % n];
        auto normal = [&](const Point& a, const Point& b) {
            float ex = b.x - a.x, ey = b.y - a.y;
            const float l = std::sqrt(ex * ex + ey * ey) + 1e-6f;
            ex /= l;
            ey /= l;
            return ccw ? Point{ey, -ex} : Point{-ey, ex};
        };
        const Point n0 = normal(p0, p1), n1 = normal(p1, p2);
        Point m{n0.x + n1.x, n0.y + n1.y};
        const float ml = std::sqrt(m.x * m.x + m.y * m.y) + 1e-6f;
        m.x /= ml;
        m.y /= ml;
        const float d = std::max(0.35f, m.x * n1.x + m.y * n1.y);
        const float len = std::min(f / d, f * 3.f);
        outer[i] = {p1.x + m.x * len, p1.y + m.y * len};
    }
    glBegin(GL_TRIANGLE_STRIP);
    for (std::size_t i = 0; i <= n; ++i) {
        color(c);
        vertex(pts[i % n].x, pts[i % n].y);
        color(c.alpha(0.f));
        vertex(outer[i % n].x, outer[i % n].y);
    }
    glEnd();
}

void Renderer::areaUnder(const std::vector<Point>& pts, float baseline, Color top, Color bottom) {
    if (pts.size() < 2) return;
    glBegin(GL_TRIANGLE_STRIP);
    for (const Point& p : pts) {
        color(top);
        vertex(p.x, p.y);
        color(bottom);
        vertex(p.x, baseline);
    }
    glEnd();
}

void Renderer::diamond(float cx, float cy, float radius, Color c) {
    polygon({{cx, cy - radius}, {cx + radius, cy}, {cx, cy + radius}, {cx - radius, cy}}, c);
}

void Renderer::image(unsigned int texture, const Rect& r, float u0, float v0, float u1, float v1, Color tint) {
    if (!texture) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    color(tint);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0);
    vertex(r.x, r.y);
    glTexCoord2f(u1, v0);
    vertex(r.r(), r.y);
    glTexCoord2f(u1, v1);
    vertex(r.r(), r.b());
    glTexCoord2f(u0, v1);
    vertex(r.x, r.b());
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void Renderer::beginGlyphs(unsigned int texture, Color c) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    color(c);
    glBegin(GL_QUADS);
}

void Renderer::glyph(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1) {
    glTexCoord2f(u0, v0);
    glVertex2f(x0 + ox_ * scale_, y0 + oy_ * scale_);
    glTexCoord2f(u1, v0);
    glVertex2f(x1 + ox_ * scale_, y0 + oy_ * scale_);
    glTexCoord2f(u1, v1);
    glVertex2f(x1 + ox_ * scale_, y1 + oy_ * scale_);
    glTexCoord2f(u0, v1);
    glVertex2f(x0 + ox_ * scale_, y1 + oy_ * scale_);
}

void Renderer::endGlyphs() {
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

// ── Backdrop blur ────────────────────────────────────────────────────────────────────────────

void Renderer::ensureBlurTargets() {
    if (blurTex_[0] && blurW_ == pixelW_ && blurH_ == pixelH_) return;
    releaseResources();
    blurW_ = pixelW_;
    blurH_ = pixelH_;
    glGenTextures(3, blurTex_);
    const int sizes[3][2] = {{pixelW_, pixelH_}, {std::max(1, pixelW_ / 4), std::max(1, pixelH_ / 4)},
                             {std::max(1, pixelW_ / 10), std::max(1, pixelH_ / 10)}};
    for (int i = 0; i < 3; ++i) {
        glBindTexture(GL_TEXTURE_2D, blurTex_[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, sizes[i][0], sizes[i][1], 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    }
}

void Renderer::backdrop(float amount, float blurStrength, Color tint) {
    amount = std::clamp(amount, 0.f, 1.f);
    if (amount <= 0.001f) return;
    if (blurStrength > 0.01f) {
        ensureBlurTargets();
        const int w0 = pixelW_, h0 = pixelH_;
        const int w1 = std::max(1, w0 / 4), h1 = std::max(1, h0 / 4);
        const int w2 = std::max(1, w0 / 10), h2 = std::max(1, h0 / 10);
        glDisable(GL_SCISSOR_TEST);
        // Draws `tex` (covering the full texture) into the window-space rectangle [0,w]x[0,h]
        // (bottom-left origin) with an optional texel offset, as a running average over `taps`.
        auto drawInto = [&](unsigned int tex, int texW, int texH, int w, int h, float alpha, float offU, float offV) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tex);
            glColor4f(1.f, 1.f, 1.f, alpha);
            const float du = offU / texW, dv = offV / texH;
            const float y0 = static_cast<float>(pixelH_ - h);  // screen-space top of the target
            glBegin(GL_QUADS);
            glTexCoord2f(0.f + du, 1.f + dv);
            glVertex2f(0.f, y0);
            glTexCoord2f(1.f + du, 1.f + dv);
            glVertex2f(static_cast<float>(w), y0);
            glTexCoord2f(1.f + du, 0.f + dv);
            glVertex2f(static_cast<float>(w), static_cast<float>(pixelH_));
            glTexCoord2f(0.f + du, 0.f + dv);
            glVertex2f(0.f, static_cast<float>(pixelH_));
            glEnd();
            glDisable(GL_TEXTURE_2D);
        };
        const float taps[4][2] = {{-0.75f, -0.75f}, {0.75f, -0.75f}, {-0.75f, 0.75f}, {0.75f, 0.75f}};
        glBindTexture(GL_TEXTURE_2D, blurTex_[0]);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w0, h0);
        for (int i = 0; i < 4; ++i) drawInto(blurTex_[0], w0, h0, w1, h1, 1.f / (i + 1), taps[i][0] * 2.f, taps[i][1] * 2.f);
        glBindTexture(GL_TEXTURE_2D, blurTex_[1]);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w1, h1);
        for (int i = 0; i < 4; ++i) drawInto(blurTex_[1], w1, h1, w2, h2, 1.f / (i + 1), taps[i][0], taps[i][1]);
        glBindTexture(GL_TEXTURE_2D, blurTex_[2]);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w2, h2);
        // Restore the sharp frame where the intermediate passes wrote, then cross-fade the blur.
        drawInto(blurTex_[0], w0, h0, w0, h0, 1.f, 0.f, 0.f);
        const float strength = std::clamp(blurStrength, 0.f, 1.f) * amount;
        for (int i = 0; i < 4; ++i) {
            // Sequential blends chosen so the result is (1-s)·sharp + s·mean(4 jittered taps).
            const float a = (strength * 0.25f) / std::max(1e-4f, 1.f - (3 - i) * strength * 0.25f);
            drawInto(blurTex_[2], w2, h2, w0, h0, a, taps[i][0] * 0.5f, taps[i][1] * 0.5f);
        }
        applyClip();
    }
    rect({0, 0, width(), height()}, tint.alpha(amount));
}

}  // namespace vigil::ui
