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
    void circle(float cx, float cy, float rad) const { r.ring(x + cx * s, y + cy * s, rad * s, w, c); }
    void dot(float cx, float cy, float rad) const { r.circle(x + cx * s, y + cy * s, rad * s, c); }
    // Arc in degrees, 0 = up, clockwise.
    void arc(float cx, float cy, float rad, float d0, float d1) const {
        r.arc(x + cx * s, y + cy * s, rad * s, w, d0 * kPi / 180.f, d1 * kPi / 180.f, c, true);
    }
    void rrect(float x0, float y0, float x1, float y1, float rad) const {
        r.roundRectStroke({x + x0 * s - w * 0.5f, y + y0 * s - w * 0.5f, (x1 - x0) * s + w, (y1 - y0) * s + w}, rad * s + w * 0.5f, w, c);
    }
};

}  // namespace

void drawIcon(Renderer& r, Icon icon, float x, float y, float size, Color c, float stroke) {
    const float s = size / 20.f;
    const Pen P{r, x, y, s, stroke * std::max(0.75f, size / 20.f), c};
    switch (icon) {
        case Icon::Overview:  // radar: rings + sweep + blip
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
        case Icon::Evidence:  // timeline lanes with marks
            P.line(3, 5.5f, 17, 5.5f);
            P.line(3, 10, 17, 10);
            P.line(3, 14.5f, 17, 14.5f);
            P.dot(7, 5.5f, 1.7f);
            P.dot(13, 10, 1.7f);
            P.dot(9.5f, 14.5f, 1.7f);
            break;
        case Icon::Players:
            P.circle(10, 7, 3.3f);
            P.arc(10, 18.5f, 6.5f, -62, 62);
            break;
        case Icon::Settings:  // sliders
            P.line(5, 3, 5, 17);
            P.line(10, 3, 10, 17);
            P.line(15, 3, 15, 17);
            P.dot(5, 12.5f, 2.1f);
            P.dot(10, 6.5f, 2.1f);
            P.dot(15, 10.5f, 2.1f);
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
    }
}

std::vector<Point> logoOutline(int seg) {
    // Unit square. Outer silhouette: straight lines (precision). Inner notch: a quadratic arc.
    const Point topLeftOuter{0.035f, 0.105f};
    const Point topLeftInner{0.315f, 0.105f};
    const Point topRightInner{0.685f, 0.105f};
    const Point topRightOuter{0.965f, 0.105f};
    const Point bottom{0.5f, 0.935f};
    const Point control{0.5f, 0.70f};
    std::vector<Point> pts;
    pts.push_back(topLeftOuter);
    pts.push_back(topLeftInner);
    for (int i = 1; i < seg; ++i) {
        const float t = static_cast<float>(i) / seg;
        const float u = 1.f - t;
        pts.push_back({u * u * topLeftInner.x + 2 * u * t * control.x + t * t * topRightInner.x,
                       u * u * topLeftInner.y + 2 * u * t * control.y + t * t * topRightInner.y});
    }
    pts.push_back(topRightInner);
    pts.push_back(topRightOuter);
    pts.push_back(bottom);
    return pts;
}

Point logoDotCenter() { return {0.5f, 0.30f}; }
float logoDotRadius() { return 0.085f; }

void drawLogo(Renderer& r, float x, float y, float size, Color body, Color dot) {
    std::vector<Point> pts = logoOutline(28);
    for (Point& p : pts) { p.x = x + p.x * size; p.y = y + p.y * size; }
    r.polygon(pts, body);
    const Point d = logoDotCenter();
    r.circle(x + d.x * size, y + d.y * size, logoDotRadius() * size, dot);
}

}  // namespace vigil::ui
