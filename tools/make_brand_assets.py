#!/usr/bin/env python3
"""Generate Vigil brand assets from the single source of truth for the mark's geometry.

The construction mirrors src/ui/icons.cpp (logoOutline / logoDotCenter / logoDotRadius), so the
in-app mark, the SVGs and the application icon are identical.

Outputs (assets/brand/):
  vigil-mark.svg        mark, ink body + iris focus point (for light or dark backgrounds via CSS)
  vigil-mark-mono.svg   single-color mark (currentColor)
  vigil-lockup.svg      mark + VIGIL wordmark
  vigil-app-icon.svg    rounded-square application icon
  vigil-app-icon.png    512 px raster of the application icon
  vigil.ico             16-256 px icon for the launcher
Standard library only.
"""

from __future__ import annotations

import math
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "brand"

INK = (0xE8, 0xEC, 0xF3)
IRIS = (0x8F, 0xA8, 0xFF)
BG_TOP = (0x17, 0x1C, 0x29)
BG_BOTTOM = (0x0C, 0x0F, 0x15)

# ── Geometry (unit square) ───────────────────────────────────────────────────────────────────
A = (0.04, 0.10)          # outer top-left
B = (0.96, 0.10)          # outer top-right
APEX = (0.5, 0.94)
ARM = 0.20                # arm width, perpendicular to the outer edge
DOT = (0.5, 0.31)         # point of light; also the centre of the lid arc
DOT_R = 0.064
FILLET = 0.022            # outer top corners
APEX_FILLET = 0.03


def _sub(a, b): return (a[0] - b[0], a[1] - b[1])
def _add(a, b): return (a[0] + b[0], a[1] + b[1])
def _mul(a, s): return (a[0] * s, a[1] * s)
def _dot(a, b): return a[0] * b[0] + a[1] * b[1]
def _norm(a):
    l = math.hypot(*a)
    return (a[0] / l, a[1] / l)


def construction():
    """Mirrors logoOutline() in src/ui/icons.cpp."""
    dl = _norm(_sub(APEX, A))
    inward = (dl[1], -dl[0])
    o = _add(A, _mul(inward, ARM))
    inner_l = _add(o, _mul(dl, (A[1] - o[1]) / dl[1]))
    inner_r = (1.0 - inner_l[0], inner_l[1])
    along = _dot(_sub(DOT, inner_l), dl)
    tan_l = _add(inner_l, _mul(dl, along))
    tan_r = (1.0 - tan_l[0], tan_l[1])
    lid_r = math.hypot(*_sub(DOT, tan_l))
    return inner_l, inner_r, tan_l, tan_r, lid_r


def fillet(a, c, b, radius):
    """Circular fillet at corner c between neighbours a and b: (start, end, centre)."""
    u, v = _norm(_sub(a, c)), _norm(_sub(b, c))
    half = math.acos(max(-1.0, min(1.0, _dot(u, v)))) * 0.5
    d = radius / math.tan(half)
    p0, p1 = _add(c, _mul(u, d)), _add(c, _mul(v, d))
    centre = _add(c, _mul(_norm(_add(u, v)), radius / math.sin(half)))
    return p0, p1, centre


def _arc_points(centre, r, a0, a1, seg):
    da = a1 - a0
    while da > math.pi: da -= 2 * math.pi
    while da < -math.pi: da += 2 * math.pi
    return [(centre[0] + r * math.cos(a0 + da * i / seg), centre[1] + r * math.sin(a0 + da * i / seg)) for i in range(seg + 1)]


def outline(segments: int = 48) -> list[tuple[float, float]]:
    inner_l, inner_r, tan_l, tan_r, lid_r = construction()
    pts = []
    def add_fillet(a, c, b, r):
        p0, p1, ce = fillet(a, c, b, r)
        pts.extend(_arc_points(ce, r, math.atan2(p0[1] - ce[1], p0[0] - ce[0]), math.atan2(p1[1] - ce[1], p1[0] - ce[0]), 8))
    add_fillet(APEX, A, inner_l, FILLET)
    pts.append(inner_l)
    a_l = math.atan2(tan_l[1] - DOT[1], tan_l[0] - DOT[0])
    a_r = math.pi - a_l
    for i in range(segments + 1):
        t = a_l + (a_r - a_l) * i / segments
        pts.append((DOT[0] + lid_r * math.cos(t), DOT[1] + lid_r * math.sin(t)))
    pts.append(inner_r)
    add_fillet(inner_r, B, APEX, FILLET)
    add_fillet(B, APEX, A, APEX_FILLET)
    return pts


def svg_path(scale: float = 100.0, ox: float = 0.0, oy: float = 0.0) -> str:
    """The same construction as SVG path commands with true arcs."""
    f = lambda v: f"{v:.3f}".rstrip("0").rstrip(".")
    P = lambda p: f"{f(ox + p[0] * scale)} {f(oy + p[1] * scale)}"
    R = lambda r: f(r * scale)
    inner_l, inner_r, tan_l, tan_r, lid_r = construction()
    a0, a1, _ = fillet(APEX, A, inner_l, FILLET)
    b0, b1, _ = fillet(inner_r, B, APEX, FILLET)
    p0, p1, _ = fillet(B, APEX, A, APEX_FILLET)
    # The polygon runs clockwise on screen: convex fillets sweep clockwise (1); the lid is concave (0).
    return (f"M{P(a0)} A{R(FILLET)} {R(FILLET)} 0 0 1 {P(a1)} L{P(inner_l)} L{P(tan_l)} "
            f"A{R(lid_r)} {R(lid_r)} 0 0 0 {P(tan_r)} L{P(inner_r)} L{P(b0)} A{R(FILLET)} {R(FILLET)} 0 0 1 {P(b1)} "
            f"L{P(p0)} A{R(APEX_FILLET)} {R(APEX_FILLET)} 0 0 1 {P(p1)} Z")


def hexc(c: tuple[int, int, int]) -> str:
    return "#%02X%02X%02X" % c


# ── SVG ──────────────────────────────────────────────────────────────────────────────────────

def write_svgs() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "vigil-mark.svg").write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" role="img" aria-label="Vigil">\n'
        f'  <path d="{svg_path()}" fill="{hexc(INK)}"/>\n'
        f'  <circle cx="{DOT[0] * 100:g}" cy="{DOT[1] * 100:g}" r="{DOT_R * 100:g}" fill="{hexc(IRIS)}"/>\n'
        f'</svg>\n', encoding="utf-8")
    (OUT / "vigil-mark-mono.svg").write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" role="img" aria-label="Vigil">\n'
        f'  <path d="{svg_path()}" fill="currentColor"/>\n'
        f'  <circle cx="{DOT[0] * 100:g}" cy="{DOT[1] * 100:g}" r="{DOT_R * 100:g}" fill="currentColor"/>\n'
        f'</svg>\n', encoding="utf-8")
    (OUT / "vigil-lockup.svg").write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 420 100" role="img" aria-label="Vigil">\n'
        f'  <path d="{svg_path(84, 8, 8)}" fill="{hexc(INK)}"/>\n'
        f'  <circle cx="{8 + DOT[0] * 84:.2f}" cy="{8 + DOT[1] * 84:.2f}" r="{DOT_R * 84:.2f}" fill="{hexc(IRIS)}"/>\n'
        f'  <text x="118" y="67" fill="{hexc(INK)}" font-family="Inter, \'Segoe UI\', system-ui, sans-serif" '
        f'font-weight="600" font-size="46" letter-spacing="14">VIGIL</text>\n'
        f'</svg>\n', encoding="utf-8")
    (OUT / "vigil-app-icon.svg").write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" role="img" aria-label="Vigil">\n'
        f'  <defs><linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">'
        f'<stop offset="0" stop-color="{hexc(BG_TOP)}"/><stop offset="1" stop-color="{hexc(BG_BOTTOM)}"/></linearGradient></defs>\n'
        f'  <rect x="8" y="8" width="240" height="240" rx="56" fill="url(#bg)"/>\n'
        f'  <rect x="8.5" y="8.5" width="239" height="239" rx="55.5" fill="none" stroke="#FFFFFF" stroke-opacity="0.08"/>\n'
        f'  <path d="{svg_path(150, 53, 58)}" fill="{hexc(INK)}"/>\n'
        f'  <circle cx="{53 + DOT[0] * 150:.2f}" cy="{58 + DOT[1] * 150:.2f}" r="{DOT_R * 150:.2f}" fill="{hexc(IRIS)}"/>\n'
        f'</svg>\n', encoding="utf-8")


# ── Rasterizer (scanline coverage with supersampling) ────────────────────────────────────────

def coverage_polygon(poly: list[tuple[float, float]], size: int, ss: int) -> list[float]:
    cov = [0.0] * (size * size)
    n = len(poly)
    step = 1.0 / ss
    for sy in range(size * ss):
        y = (sy + 0.5) * step
        xs = []
        for i in range(n):
            x0, y0 = poly[i]
            x1, y1 = poly[(i + 1) % n]
            if (y0 <= y < y1) or (y1 <= y < y0):
                xs.append(x0 + (y - y0) * (x1 - x0) / (y1 - y0))
        xs.sort()
        row = (sy // ss) * size
        for k in range(0, len(xs) - 1, 2):
            a, b = xs[k], xs[k + 1]
            # Accumulate horizontal coverage per pixel with sub-pixel precision.
            px0 = max(0, int(math.floor(a)))
            px1 = min(size - 1, int(math.floor(b)))
            for px in range(px0, px1 + 1):
                left = max(a, px)
                right = min(b, px + 1)
                if right > left:
                    cov[row + px] += (right - left) / ss
    return [min(1.0, c) for c in cov]


def circle_poly(cx: float, cy: float, r: float, seg: int = 64) -> list[tuple[float, float]]:
    return [(cx + r * math.cos(2 * math.pi * i / seg), cy + r * math.sin(2 * math.pi * i / seg)) for i in range(seg)]


def rounded_rect_poly(x: float, y: float, w: float, h: float, r: float, seg: int = 16) -> list[tuple[float, float]]:
    pts = []
    corners = [(x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)]
    for cx, cy, start in corners:
        for i in range(seg + 1):
            a = math.radians(start + 90 * i / seg)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def render_icon(size: int) -> bytes:
    ss = 4 if size >= 64 else 6
    inset = size * (8 / 256)
    rr = size * (56 / 256) if size > 24 else size * 0.22
    bg = coverage_polygon(rounded_rect_poly(inset, inset, size - 2 * inset, size - 2 * inset, rr), size, ss)
    # Small icons get a larger mark so the V stays legible.
    mark_scale = size * (150 / 256 if size > 32 else 0.70)
    ox = (size - mark_scale) / 2
    oy = size * (58 / 256) if size > 32 else (size - mark_scale) / 2 + size * 0.02
    poly = [(ox + x * mark_scale, oy + y * mark_scale) for x, y in outline(64)]
    mark = coverage_polygon(poly, size, ss)
    dot = coverage_polygon(circle_poly(ox + DOT[0] * mark_scale, oy + DOT[1] * mark_scale, DOT_R * mark_scale), size, ss)
    pixels = bytearray()
    for py in range(size):
        t = py / max(1, size - 1)
        base = tuple(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t for i in range(3))
        for px in range(size):
            i = py * size + px
            a = bg[i]
            c = list(base)
            for layer, color in ((mark[i], INK), (dot[i], IRIS)):
                c = [c[k] + (color[k] - c[k]) * layer for k in range(3)]
            pixels += bytes((int(c[0] + 0.5), int(c[1] + 0.5), int(c[2] + 0.5), int(a * 255 + 0.5)))
    return bytes(pixels)


def png(size: int, rgba: bytes) -> bytes:
    raw = b"".join(b"\x00" + rgba[y * size * 4:(y + 1) * size * 4] for y in range(size))
    def chunk(t: bytes, d: bytes) -> bytes:
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def write_icons() -> None:
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [png(s, render_icon(s)) for s in sizes]
    header = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    entries = b""
    for s, img in zip(sizes, images):
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    (OUT / "vigil.ico").write_bytes(header + entries + b"".join(images))
    (OUT / "vigil-app-icon.png").write_bytes(png(512, render_icon(512)))


if __name__ == "__main__":
    write_svgs()
    write_icons()
    print("brand assets written to", OUT)
