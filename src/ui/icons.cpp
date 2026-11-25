#include "icons.h"

#include <cmath>

namespace vigil::ui {
namespace {

constexpr float kPi = 3.14159265f;

struct Pen {
    Renderer& r;
    float x, y, s, w;
    Color c;
    Point p(float gx, float gy) const { return {x + gx * s, y + gy * s}; }
    void line(float x0, float y0, float x1, float y1) const { r.line(x + x0 * s, y + y0 * s, x + x1 * s, y + y1 * s, w, c); }
    void poly(std::initializer_list<Point> pts, bool closed = false) const {
        std::vector<Point> v;
        for (const Point& q : pts) v.push_back(p(q.x, q.y));
        r.polyline(v, w, c, closed);
    }
    void fill(std::initializer_list<Point> pts) const {
        std::vector<Point> v;
        for (const Point& q : pts) v.push_back(p(q.x, q.y));
        r.polygon(v, c);
    }
    void circle(float cx, float cy, float rad) const { r.ring(x + cx * s, y + cy * s, rad * s, w, c); }
    void dot(float cx, float cy, float rad) const { r.circle(x + cx * s, y + cy * s, rad * s, c); }
    // Arc in degrees, 0 = up, clockwise.
    void arc(float cx, float cy, float rad, float d0, float d1) const {
        r.arc(x + cx * s, y + cy * s, rad * s, w, d0 * kPi / 180.f, d1 * kPi / 180.f, c, true);
    }
    void rrect(float x0, float y0, float x1, float y1, float rad) const {
        r.roundRectStroke({x + x0 * s - w * 0.5f, y + y0 * s - w * 0.5f, (x1 - x0) * s + w, (y1 - y0) * s + w}, rad * s + w * 0.5f, w, c);
    }
    void frect(float x0, float y0, float x1, float y1, float rad) const {
        r.roundRect({x + x0 * s, y + y0 * s, (x1 - x0) * s, (y1 - y0) * s}, rad * s, c);
    }
};

}  // namespace

void drawIcon(Renderer& r, Icon icon, float x, float y, float size, Color c, float stroke) {
    const float s = size / 20.f;
    const Pen P{r, x, y, s, stroke * std::max(0.75f, size / 20.f), c};
    switch (icon) {
        case Icon::Overview:  // radar: ring + sweep + blip
            P.circle(10, 10, 7.5f);
            P.arc(10, 10, 3.8f, 0, 270);
            P.line(10, 10, 15.3f, 4.7f);
            P.dot(13.4f, 12.6f, 1.25f);
            break;
        case Icon::Checks:  // reticle
            P.circle(10, 10, 6.2f);
            P.line(10, 1.8f, 10, 5.0f);
            P.line(10, 15.0f, 10, 18.2f);
            P.line(1.8f, 10, 5.0f, 10);
            P.line(15.0f, 10, 18.2f, 10);
            P.dot(10, 10, 1.4f);
            break;
        case Icon::Evidence:  // document with a folded corner and a marked line
            P.poly({{5, 2.5f}, {12, 2.5f}, {15.5f, 6}, {15.5f, 17.5f}, {5, 17.5f}}, true);
            P.poly({{12, 2.5f}, {12, 6}, {15.5f, 6}});
            P.line(7.5f, 10, 13, 10);
            P.line(7.5f, 13.5f, 11, 13.5f);
            P.dot(7.6f, 6.6f, 1.2f);
            break;
        case Icon::Players:
            P.circle(10, 7, 3.3f);
            P.arc(10, 18.5f, 6.5f, -62, 62);
            break;
        case Icon::Utilities:  // tool grid: three outlined tiles and one solid
            P.rrect(3, 3, 8.6f, 8.6f, 1.8f);
            P.rrect(11.4f, 3, 17, 8.6f, 1.8f);
            P.rrect(3, 11.4f, 8.6f, 17, 1.8f);
            P.frect(11.4f, 11.4f, 17, 17, 1.8f);
            break;
        case Icon::Settings:  // sliders
            P.line(5, 3, 5, 17);
            P.line(10, 3, 10, 17);
            P.line(15, 3, 15, 17);
            P.dot(5, 12.5f, 2.1f);
            P.dot(10, 6.5f, 2.1f);
            P.dot(15, 10.5f, 2.1f);
            break;
        case Icon::Status:  // heartbeat inside a rounded frame
            P.rrect(2.5f, 4, 17.5f, 16, 3);
            P.poly({{5, 10.5f}, {7.4f, 10.5f}, {8.8f, 7}, {11.2f, 13.5f}, {12.6f, 10.5f}, {15, 10.5f}});
            break;
        case Icon::Identity:  // ID card
            P.rrect(2.5f, 4.5f, 17.5f, 15.5f, 2.2f);
            P.circle(7.2f, 9, 1.9f);
            P.arc(7.2f, 15, 3.2f, -60, 60);
            P.line(11.5f, 8.5f, 15, 8.5f);
            P.line(11.5f, 11.5f, 14, 11.5f);
            break;
        case Icon::Combat:
        case Icon::Target:
            P.circle(10, 10, 7);
            P.circle(10, 10, 3.2f);
            P.dot(10, 10, 0.9f);
            break;
        case Icon::Movement:  // trajectory
            P.arc(10, 15, 7, -75, 75);
            P.poly({{14.6f, 6.6f}, {17.2f, 9.0f}, {16.6f, 5.2f}});
            P.dot(3.3f, 13.3f, 1.3f);
            break;
        case Icon::World:  // isometric block
            P.poly({{10, 2.6f}, {16.6f, 6.3f}, {16.6f, 13.7f}, {10, 17.4f}, {3.4f, 13.7f}, {3.4f, 6.3f}}, true);
            P.poly({{3.4f, 6.3f}, {10, 10}, {16.6f, 6.3f}});
            P.line(10, 10, 10, 17.4f);
            break;
        case Icon::KillAura:  // orbit with satellites
            P.circle(10, 10, 2.6f);
            P.arc(10, 10, 7, 20, 160);
            P.arc(10, 10, 7, 200, 340);
            P.dot(10, 3, 1.4f);
            P.dot(10, 17, 1.4f);
            break;
        case Icon::Reach:  // span with end ticks
            P.line(3, 10, 17, 10);
            P.line(3, 6.5f, 3, 13.5f);
            P.line(17, 6.5f, 17, 13.5f);
            P.line(7, 8.5f, 7, 11.5f);
            P.line(10, 8.0f, 10, 12.0f);
            P.line(13, 8.5f, 13, 11.5f);
            break;
        case Icon::AutoClicker:  // cursor + rays
            P.poly({{6, 6}, {6, 17}, {9, 14}, {11.5f, 18.5f}, {13, 17.8f}, {10.6f, 13.3f}, {14.6f, 13.3f}}, true);
            P.line(10.5f, 2.2f, 10.5f, 4.2f);
            P.line(15.2f, 4.3f, 13.8f, 5.7f);
            P.line(17.2f, 8.8f, 15.2f, 8.8f);
            break;
        case Icon::Velocity:  // impulse hitting a wall
            P.line(16, 3, 16, 17);
            P.line(3, 10, 12.5f, 10);
            P.poly({{9.5f, 6.8f}, {12.8f, 10}, {9.5f, 13.2f}});
            break;
        case Icon::AutoBlock:  // raised block (bracketed bar)
            P.poly({{6, 4}, {4, 4}, {4, 16}, {6, 16}});
            P.poly({{14, 4}, {16, 4}, {16, 16}, {14, 16}});
            P.line(10, 5, 10, 15);
            break;
        case Icon::Speed:  // gauge
            P.arc(10, 12, 7, -110, 110);
            P.line(10, 12, 14.2f, 7.4f);
            P.dot(10, 12, 1.3f);
            break;
        case Icon::BHop:  // bouncing arcs
            P.arc(6.5f, 15, 3.5f, -90, 90);
            P.arc(13.5f, 15, 3.5f, -90, 90);
            P.line(2, 17.5f, 18, 17.5f);
            P.dot(16.8f, 8, 1.3f);
            break;
        case Icon::NoSlow:  // flask
            P.poly({{8, 3}, {8, 8}, {4.2f, 15.2f}, {5.2f, 17}, {14.8f, 17}, {15.8f, 15.2f}, {12, 8}, {12, 3}});
            P.line(7, 3, 13, 3);
            P.line(6.2f, 12.5f, 13.8f, 12.5f);
            break;
        case Icon::KeepY:  // level line over blocks
            P.rrect(3, 12, 8, 17, 1);
            P.rrect(8, 12, 13, 17, 1);
            P.rrect(13, 12, 17.5f, 17, 1);
            P.line(3, 7, 17.5f, 7);
            P.dot(10, 7, 1.5f);
            break;
        case Icon::FlatScaffold:
            P.rrect(2.5f, 12, 7.5f, 17, 1);
            P.rrect(7.5f, 12, 12.5f, 17, 1);
            P.line(4, 7, 15.5f, 7);
            P.poly({{13, 4.5f}, {15.8f, 7}, {13, 9.5f}});
            break;
        case Icon::LegitScaff:  // shift key
            P.rrect(3, 4, 17, 16, 3);
            P.poly({{10, 6.5f}, {13.6f, 10.2f}, {11.6f, 10.2f}, {11.6f, 13.2f}, {8.4f, 13.2f}, {8.4f, 10.2f}, {6.4f, 10.2f}}, true);
            break;
        case Icon::Chat:  // speech bubble with lines
            P.poly({{3, 4}, {17, 4}, {17, 13.5f}, {9, 13.5f}, {5.5f, 16.8f}, {5.5f, 13.5f}, {3, 13.5f}}, true);
            P.line(6, 7.6f, 14, 7.6f);
            P.line(6, 10.4f, 11.5f, 10.4f);
            break;
        case Icon::Shop:  // shopping bag
            P.poly({{4, 7}, {16, 7}, {15.2f, 17}, {4.8f, 17}}, true);
            P.arc(10, 7, 3, -90, 90);
            P.line(7, 7, 7, 7);
            break;
        case Icon::Party:  // two people
            P.circle(7.5f, 7, 2.6f);
            P.arc(7.5f, 17, 5.2f, -60, 60);
            P.circle(13.6f, 6.2f, 2.2f);
            P.arc(13.6f, 15.4f, 4.4f, 10, 62);
            break;
        case Icon::Queue:  // a run of consecutive ids
            P.rrect(2.5f, 7, 6.5f, 13, 1.2f);
            P.rrect(8, 7, 12, 13, 1.2f);
            P.frect(13.5f, 7, 17.5f, 13, 1.2f);
            P.line(2.5f, 16, 17.5f, 16);
            break;
        case Icon::Search:
            P.circle(8.6f, 8.6f, 5.2f);
            P.line(12.5f, 12.5f, 16.8f, 16.8f);
            break;
        case Icon::Info:
            P.circle(10, 10, 7.5f);
            P.line(10, 9, 10, 14);
            P.dot(10, 6.3f, 1.1f);
            break;
        case Icon::Reset:
            P.arc(10, 10, 6.2f, 60, 360);
            P.poly({{10.0f, 1.6f}, {10.0f, 4.6f}, {13.0f, 4.6f}});
            break;
        case Icon::ChevronRight: P.poly({{8, 5}, {13, 10}, {8, 15}}); break;
        case Icon::ChevronLeft: P.poly({{12, 5}, {7, 10}, {12, 15}}); break;
        case Icon::ChevronDown: P.poly({{5, 8}, {10, 13}, {15, 8}}); break;
        case Icon::Back:
            P.line(4, 10, 16, 10);
            P.poly({{9, 5}, {4, 10}, {9, 15}});
            break;
        case Icon::ArrowRight:
            P.line(4, 10, 16, 10);
            P.poly({{11, 5}, {16, 10}, {11, 15}});
            break;
        case Icon::Close:
            P.line(5.5f, 5.5f, 14.5f, 14.5f);
            P.line(14.5f, 5.5f, 5.5f, 14.5f);
            break;
        case Icon::Check: P.poly({{4.5f, 10.5f}, {8.3f, 14.2f}, {15.5f, 6.2f}}); break;
        case Icon::Warning:
            P.poly({{10, 3}, {17.5f, 16.5f}, {2.5f, 16.5f}}, true);
            P.line(10, 8, 10, 11.6f);
            P.dot(10, 14, 1.0f);
            break;
        case Icon::Clock:
            P.circle(10, 10, 7.3f);
            P.poly({{10, 5.5f}, {10, 10}, {13.2f, 12}});
            break;
        case Icon::Pin:
            P.circle(10, 8, 4.2f);
            P.line(10, 12.2f, 10, 17.5f);
            break;
        case Icon::Export:
            P.poly({{6, 8}, {4, 8}, {4, 16.5f}, {16, 16.5f}, {16, 8}, {14, 8}});
            P.line(10, 3, 10, 12);
            P.poly({{6.8f, 6}, {10, 2.8f}, {13.2f, 6}});
            break;
        case Icon::Folder:
            P.poly({{3, 6}, {3, 16}, {17, 16}, {17, 7.5f}, {10, 7.5f}, {8.5f, 5}, {3, 5}}, true);
            break;
        case Icon::Bell:
            P.poly({{5, 14}, {5.8f, 12.5f}, {5.8f, 9}});
            P.arc(10, 9, 4.2f, -90, 90);
            P.poly({{14.2f, 9}, {14.2f, 12.5f}, {15, 14}});
            P.line(4.5f, 14, 15.5f, 14);
            P.arc(10, 15.2f, 1.8f, 100, 260);
            break;
        case Icon::Palette:
            P.arc(10, 10, 7.5f, 40, 340);
            P.dot(6.8f, 8.4f, 1.2f);
            P.dot(10, 6, 1.2f);
            P.dot(13.4f, 8.2f, 1.2f);
            P.circle(13.2f, 13.6f, 1.6f);
            break;
        case Icon::Database:
            P.arc(10, 5, 6, -90, 90);
            P.arc(10, 5, 6, 90, 270);
            P.line(4, 5, 4, 15);
            P.line(16, 5, 16, 15);
            P.arc(10, 15, 6, 90, 270);
            P.arc(10, 10, 6, 90, 270);
            break;
        case Icon::Pulse:
            P.poly({{2, 10.5f}, {6, 10.5f}, {8, 5}, {11.5f, 15.5f}, {13.5f, 10.5f}, {18, 10.5f}});
            break;
        case Icon::Filter:
            P.line(3, 5.5f, 17, 5.5f);
            P.line(5.5f, 10, 14.5f, 10);
            P.line(8, 14.5f, 12, 14.5f);
            break;
        case Icon::Plus:
            P.line(10, 4.5f, 10, 15.5f);
            P.line(4.5f, 10, 15.5f, 10);
            break;
        case Icon::Minus: P.line(4.5f, 10, 15.5f, 10); break;
        case Icon::Dot: P.dot(10, 10, 3.2f); break;
        case Icon::Bolt: P.poly({{11.5f, 2.5f}, {5, 11}, {9.6f, 11}, {8.5f, 17.5f}, {15, 9}, {10.4f, 9}}, true); break;
        case Icon::Spark:
            P.line(10, 2.5f, 10, 6.5f);
            P.line(10, 13.5f, 10, 17.5f);
            P.line(2.5f, 10, 6.5f, 10);
            P.line(13.5f, 10, 17.5f, 10);
            P.dot(10, 10, 1.6f);
            break;
        case Icon::Report:
            P.line(5, 3, 5, 17.5f);
            P.poly({{5, 4}, {15.5f, 4}, {13, 7.5f}, {15.5f, 11}, {5, 11}});
            break;
        case Icon::Layers:
            P.poly({{10, 3}, {17, 7}, {10, 11}, {3, 7}}, true);
            P.poly({{3, 10.5f}, {10, 14.5f}, {17, 10.5f}});
            P.poly({{3, 14}, {10, 18}, {17, 14}});
            break;
        case Icon::Keyboard:
            P.rrect(2, 5, 18, 15, 2.2f);
            for (int i = 0; i < 4; ++i) P.dot(5.2f + i * 3.2f, 8.4f, 0.9f);
            P.line(6.5f, 11.8f, 13.5f, 11.8f);
            break;
        case Icon::Copy:
            P.rrect(7, 7, 16.5f, 16.5f, 2);
            P.poly({{4, 12.5f}, {3.5f, 12.5f}, {3.5f, 3.5f}, {12.5f, 3.5f}, {12.5f, 4}});
            break;
        case Icon::Eye:
            P.arc(10, 17.5f, 10.5f, -50, 50);
            P.arc(10, 2.5f, 10.5f, 130, 230);
            P.circle(10, 10, 2.4f);
            break;
        case Icon::TrendUp:
            P.poly({{3, 15}, {8, 10}, {11, 13}, {17, 6.5f}});
            P.poly({{12.5f, 6.5f}, {17, 6.5f}, {17, 11}});
            break;
        case Icon::TrendDown:
            P.poly({{3, 5}, {8, 10}, {11, 7}, {17, 13.5f}});
            P.poly({{12.5f, 13.5f}, {17, 13.5f}, {17, 9}});
            break;
        case Icon::Globe:
            P.circle(10, 10, 7.3f);
            P.line(2.7f, 10, 17.3f, 10);
            P.arc(10, 10, 7.3f, 0, 0.1f);
            P.arc(16.5f, 10, 9.2f, 232, 308);
            P.arc(3.5f, 10, 9.2f, 52, 128);
            break;
        case Icon::Play: P.poly({{6.5f, 4}, {15.5f, 10}, {6.5f, 16}}, true); break;
        case Icon::Plug:
            P.line(7.5f, 2.5f, 7.5f, 6);
            P.line(12.5f, 2.5f, 12.5f, 6);
            P.poly({{5, 6}, {15, 6}, {15, 9.5f}});
            P.arc(10, 9.5f, 5, 90, 270);
            P.line(5, 6, 5, 9.5f);
            P.line(10, 14.5f, 10, 17.5f);
            break;
        case Icon::Chip:
            P.rrect(5.5f, 5.5f, 14.5f, 14.5f, 1.8f);
            P.rrect(8.3f, 8.3f, 11.7f, 11.7f, 0.6f);
            for (float k : {8.f, 12.f}) {
                P.line(k, 2.5f, k, 5.5f);
                P.line(k, 14.5f, k, 17.5f);
                P.line(2.5f, k, 5.5f, k);
                P.line(14.5f, k, 17.5f, k);
            }
            break;
        case Icon::Mask:  // a nick: masked face
            P.poly({{3, 7}, {17, 7}, {16, 11.5f}, {12.5f, 12.5f}, {10, 10.8f}, {7.5f, 12.5f}, {4, 11.5f}}, true);
            P.dot(7.3f, 9.3f, 1.1f);
            P.dot(12.7f, 9.3f, 1.1f);
            break;
    }
}

// ── The Vigil mark ───────────────────────────────────────────────────────────────────────────
//
// Construction (unit square, y down):
//   outer V      A(0.04, 0.10) → apex P(0.5, 0.94) → B(0.96, 0.10)
//   arm width    t = 0.20, measured perpendicular to the outer edge; inner edges are parallel
//   eye          the dot D(0.5, 0.31) is the centre of an arc tangent to both inner edges, so the
//                notch is a lower lid cradling the point of light; dot radius 0.064
//   finish       outer corners rounded by 0.022, apex by 0.03 (optical, invisible at 16 px)

namespace {
struct V2 {
    double x, y;
};
V2 add(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
V2 sub(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
V2 mul(V2 a, double s) { return {a.x * s, a.y * s}; }
double dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
V2 norm(V2 a) { const double l = std::sqrt(dot(a, a)); return {a.x / l, a.y / l}; }

constexpr V2 kA{0.04, 0.10}, kB{0.96, 0.10}, kApex{0.5, 0.94}, kDot{0.5, 0.31};
constexpr double kArm = 0.20, kDotR = 0.064;

// Rounds the corner at `c` between neighbours `a` and `b` with a circular fillet.
void fillet(std::vector<Point>& out, V2 a, V2 c, V2 b, double radius, int segs) {
    const V2 u = norm(sub(a, c)), v = norm(sub(b, c));
    const double cosT = std::fmax(-1.0, std::fmin(1.0, dot(u, v)));
    const double half = std::acos(cosT) * 0.5;
    const double dist = radius / std::tan(half);
    const V2 p0 = add(c, mul(u, dist)), p1 = add(c, mul(v, dist));
    const V2 bis = norm(add(u, v));
    const V2 center = add(c, mul(bis, radius / std::sin(half)));
    double a0 = std::atan2(p0.y - center.y, p0.x - center.x);
    double a1 = std::atan2(p1.y - center.y, p1.x - center.x);
    double da = a1 - a0;
    while (da > 3.14159265358979) da -= 2 * 3.14159265358979;
    while (da < -3.14159265358979) da += 2 * 3.14159265358979;
    for (int i = 0; i <= segs; ++i) {
        const double t = a0 + da * i / segs;
        out.push_back({static_cast<float>(center.x + radius * std::cos(t)), static_cast<float>(center.y + radius * std::sin(t))});
    }
}
}  // namespace

std::vector<Point> logoOutline(int arcSegments) {
    const V2 dl = norm(sub(kApex, kA));
    const V2 inward{dl.y, -dl.x};  // perpendicular, toward the inside of the left arm
    // Inner left edge: parallel to the outer edge, offset by the arm width; meet the top line.
    const V2 o = add(kA, mul(inward, kArm));
    const V2 innerTopL = add(o, mul(dl, (kA.y - o.y) / dl.y));
    const V2 innerTopR{1.0 - innerTopL.x, innerTopL.y};
    // Lid: arc centred on the dot, tangent to both inner edges.
    const V2 rel = sub(kDot, innerTopL);
    const double along = dot(rel, dl);
    const V2 tangentL = add(innerTopL, mul(dl, along));
    const double lidR = std::sqrt(dot(sub(kDot, tangentL), sub(kDot, tangentL)));
    const double aL = std::atan2(tangentL.y - kDot.y, tangentL.x - kDot.x);   // lower-left
    const double aR = 3.14159265358979 - aL;                                    // mirror

    std::vector<Point> pts;
    const int fil = 6;
    fillet(pts, kApex, kA, innerTopL, 0.022, fil);
    pts.push_back({static_cast<float>(innerTopL.x), static_cast<float>(innerTopL.y)});
    for (int i = 0; i <= arcSegments; ++i) {
        const double t = aL + (aR - aL) * i / arcSegments;   // sweeps through the bottom
        pts.push_back({static_cast<float>(kDot.x + lidR * std::cos(t)), static_cast<float>(kDot.y + lidR * std::sin(t))});
    }
    pts.push_back({static_cast<float>(innerTopR.x), static_cast<float>(innerTopR.y)});
    fillet(pts, innerTopR, kB, kApex, 0.022, fil);
    fillet(pts, kB, kApex, kA, 0.03, fil);
    return pts;
}

Point logoDotCenter() { return {static_cast<float>(kDot.x), static_cast<float>(kDot.y)}; }
float logoDotRadius() { return static_cast<float>(kDotR); }

void drawLogo(Renderer& r, float x, float y, float size, Color body, Color dot) {
    std::vector<Point> pts = logoOutline(28);
    for (Point& p : pts) { p.x = x + p.x * size; p.y = y + p.y * size; }
    r.polygon(pts, body);
    const Point d = logoDotCenter();
    r.circle(x + d.x * size, y + d.y * size, logoDotRadius() * size, dot);
}

}  // namespace vigil::ui
