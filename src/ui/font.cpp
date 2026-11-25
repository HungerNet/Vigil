#include "font.h"

#include "resources.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace vigil::ui {

unsigned int nextCodepoint(std::string_view s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    unsigned int cp = c;
    int extra = 0;
    if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
    else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
    else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
    ++i;
    for (int k = 0; k < extra && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
    return cp;
}

namespace {

struct Glyph {
    float advance = 0;     // px
    int bx = 0, by = 0;    // bearing: left, top above baseline (px)
    int w = 0, h = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};

constexpr int kAtlas = 1024;

struct Atlas {
    HFONT font = nullptr;
    int pixelSize = 0;
    int ascent = 0, descent = 0, lineGap = 0;
    GLuint tex = 0;
    std::vector<unsigned char> pixels;
    int penX = 1, penY = 1, rowH = 0;
    bool dirty = false;
    std::unordered_map<unsigned int, Glyph> glyphs;
    std::map<std::pair<unsigned int, unsigned int>, int> kerning;
};

HMODULE thisModule() {
    HMODULE m = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&nextCodepoint), &m);
    return m;
}

}  // namespace

struct Fonts::Impl {
    float scale = 1.f;
    bool registered = false;
    std::vector<HANDLE> memFonts;
    std::map<std::pair<int, int>, std::unique_ptr<Atlas>> atlases;
    HDC dc = nullptr;

    ~Impl() {
        for (auto& kv : atlases) if (kv.second->font) DeleteObject(kv.second->font);
        for (HANDLE h : memFonts) RemoveFontMemResourceEx(h);
        if (dc) DeleteDC(dc);
    }

    void registerFonts() {
        if (registered) return;
        registered = true;
        HMODULE mod = thisModule();
        const int ids[] = {VIGIL_RES_FONT_REGULAR, VIGIL_RES_FONT_MEDIUM, VIGIL_RES_FONT_SEMIBOLD, VIGIL_RES_FONT_DISPLAY};
        for (int id : ids) {
            HRSRC res = FindResourceA(mod, MAKEINTRESOURCEA(id), MAKEINTRESOURCEA(10) /*RT_RCDATA*/);
            if (!res) continue;
            HGLOBAL data = LoadResource(mod, res);
            const DWORD size = SizeofResource(mod, res);
            void* ptr = data ? LockResource(data) : nullptr;
            DWORD count = 0;
            if (ptr && size) {
                if (HANDLE h = AddFontMemResourceEx(ptr, size, nullptr, &count)) memFonts.push_back(h);
            }
        }
        dc = CreateCompatibleDC(nullptr);
    }

    HFONT createFont(Face face, int px) {
        struct Candidate { const wchar_t* name; int weight; };
        std::vector<Candidate> c;
        switch (face) {
            case Face::Regular: c = {{L"Inter", FW_NORMAL}, {L"Segoe UI", FW_NORMAL}}; break;
            case Face::Medium: c = {{L"Inter Medium", FW_DONTCARE}, {L"Inter", FW_MEDIUM}, {L"Segoe UI Semibold", FW_DONTCARE}}; break;
            case Face::SemiBold: c = {{L"Inter SemiBold", FW_DONTCARE}, {L"Inter", FW_SEMIBOLD}, {L"Segoe UI Semibold", FW_DONTCARE}}; break;
            case Face::Display:
                c = {{L"Inter Display SemiBold", FW_DONTCARE}, {L"Inter Display", FW_SEMIBOLD}, {L"Inter SemiBold", FW_DONTCARE},
                     {L"Segoe UI Semibold", FW_DONTCARE}};
                break;
            case Face::Mono: c = {{L"Cascadia Mono", FW_NORMAL}, {L"Consolas", FW_NORMAL}}; break;
        }
        for (const Candidate& k : c) {
            HFONT f = CreateFontW(-px, 0, 0, 0, k.weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
                                  CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, k.name);
            if (!f) continue;
            HGDIOBJ old = SelectObject(dc, f);
            wchar_t got[64] = {};
            GetTextFaceW(dc, 64, got);
            SelectObject(dc, old);
            if (_wcsicmp(got, k.name) == 0) return f;
            DeleteObject(f);
        }
        return CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }

    Atlas& atlas(const TextStyle& s) {
        registerFonts();
        const int px = std::max(6, static_cast<int>(std::lround(s.size * scale)));
        auto key = std::make_pair(static_cast<int>(s.face), px);
        auto it = atlases.find(key);
        if (it != atlases.end()) return *it->second;
        auto a = std::make_unique<Atlas>();
        a->pixelSize = px;
        a->font = createFont(s.face, px);
        a->pixels.assign(static_cast<std::size_t>(kAtlas) * kAtlas, 0);
        HGDIOBJ old = SelectObject(dc, a->font);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        a->ascent = tm.tmAscent;
        a->descent = tm.tmDescent;
        a->lineGap = tm.tmExternalLeading;
        const DWORD pairs = GetKerningPairsW(dc, 0, nullptr);
        if (pairs > 0 && pairs < 100000) {
            std::vector<KERNINGPAIR> kp(pairs);
            GetKerningPairsW(dc, pairs, kp.data());
            for (const auto& p : kp) a->kerning[{p.wFirst, p.wSecond}] = p.iKernAmount;
        }
        SelectObject(dc, old);
        Atlas& ref = *a;
        atlases[key] = std::move(a);
        return ref;
    }

    const Glyph& glyph(Atlas& a, unsigned int cp) {
        auto it = a.glyphs.find(cp);
        if (it != a.glyphs.end()) return it->second;
        Glyph g;
        HGDIOBJ old = SelectObject(dc, a.font);
        GLYPHMETRICS gm{};
        const MAT2 identity{{0, 1}, {0, 0}, {0, 0}, {0, 1}};
        const DWORD size = GetGlyphOutlineW(dc, cp, GGO_GRAY8_BITMAP, &gm, 0, nullptr, &identity);
        g.advance = static_cast<float>(gm.gmCellIncX);
        if (size != GDI_ERROR && size > 0) {
            std::vector<unsigned char> buf(size);
            GetGlyphOutlineW(dc, cp, GGO_GRAY8_BITMAP, &gm, size, buf.data(), &identity);
            const int w = static_cast<int>(gm.gmBlackBoxX), h = static_cast<int>(gm.gmBlackBoxY);
            const int pitch = (w + 3) & ~3;
            if (a.penX + w + 1 >= kAtlas) { a.penX = 1; a.penY += a.rowH + 1; a.rowH = 0; }
            if (a.penY + h + 1 < kAtlas) {
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        const float cov = buf[static_cast<std::size_t>(y * pitch + x)] / 64.f;
                        // Slight gamma lift keeps light-on-dark text from looking thin.
                        const float v = std::pow(std::clamp(cov, 0.f, 1.f), 0.85f);
                        a.pixels[static_cast<std::size_t>((a.penY + y) * kAtlas + a.penX + x)] =
                            static_cast<unsigned char>(v * 255.f + 0.5f);
                    }
                }
                g.w = w;
                g.h = h;
                g.bx = gm.gmptGlyphOrigin.x;
                g.by = gm.gmptGlyphOrigin.y;
                g.u0 = static_cast<float>(a.penX) / kAtlas;
                g.v0 = static_cast<float>(a.penY) / kAtlas;
                g.u1 = static_cast<float>(a.penX + w) / kAtlas;
                g.v1 = static_cast<float>(a.penY + h) / kAtlas;
                a.penX += w + 1;
                a.rowH = std::max(a.rowH, h);
                a.dirty = true;
            }
        }
        SelectObject(dc, old);
        return a.glyphs.emplace(cp, g).first->second;
    }

    void upload(Atlas& a) {
        if (!a.tex) {
            glGenTextures(1, &a.tex);
            glBindTexture(GL_TEXTURE_2D, a.tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, kAtlas, kAtlas, 0, GL_ALPHA, GL_UNSIGNED_BYTE, a.pixels.data());
            a.dirty = false;
            return;
        }
        if (a.dirty) {
            glBindTexture(GL_TEXTURE_2D, a.tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kAtlas, kAtlas, GL_ALPHA, GL_UNSIGNED_BYTE, a.pixels.data());
            a.dirty = false;
        }
    }
};

Fonts::Fonts() : d_(std::make_unique<Impl>()) {}
Fonts::~Fonts() = default;

bool Fonts::initialize() {
    d_->registerFonts();
    return !d_->memFonts.empty();
}

void Fonts::releaseGraphics() {
    for (auto& kv : d_->atlases) {
        if (kv.second->tex) glDeleteTextures(1, &kv.second->tex);
        kv.second->tex = 0;
        kv.second->dirty = true;
    }
}

void Fonts::setScale(float scale) {
    if (std::fabs(scale - d_->scale) < 1e-3f) return;
    d_->scale = scale;
}

float Fonts::lineHeight(const TextStyle& s) {
    Atlas& a = d_->atlas(s);
    return static_cast<float>(a.ascent + a.descent) / d_->scale;
}

float Fonts::width(const TextStyle& s, std::string_view text) {
    Atlas& a = d_->atlas(s);
    float w = 0.f;
    unsigned int prev = 0;
    const float tracking = s.tracking * d_->scale;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned int cp = nextCodepoint(text, i);
        const Glyph& g = d_->glyph(a, cp);
        if (prev) {
            auto k = a.kerning.find({prev, cp});
            if (k != a.kerning.end()) w += static_cast<float>(k->second);
        }
        w += g.advance + tracking;
        prev = cp;
    }
    if (!text.empty()) w -= tracking;
    return w / d_->scale;
}

std::string Fonts::ellipsize(const TextStyle& s, std::string_view text, float maxWidth) {
    if (maxWidth <= 0.f || width(s, text) <= maxWidth) return std::string(text);
    const std::string dots = "\xE2\x80\xA6";
    const float dw = width(s, dots);
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t start = i;
        nextCodepoint(text, i);
        std::string candidate = out + std::string(text.substr(start, i - start));
        if (width(s, candidate) + dw > maxWidth) break;
        out = candidate;
    }
    return out + dots;
}

float Fonts::draw(Renderer& r, const TextStyle& s, float x, float y, std::string_view text, Color c, Align align,
                  float maxWidth) {
    if (text.empty() || c.a <= 0.f) return 0.f;
    std::string clipped;
    if (maxWidth > 0.f) {
        clipped = ellipsize(s, text, maxWidth);
        text = clipped;
    }
    const float w = width(s, text);
    if (align == Align::Center) x -= w * 0.5f;
    else if (align == Align::Right) x -= w;
    Atlas& a = d_->atlas(s);
    // Rasterize every glyph first so the texture is current before drawing.
    for (std::size_t i = 0; i < text.size();) d_->glyph(a, nextCodepoint(text, i));
    d_->upload(a);
    const float sc = d_->scale;
    float pen = std::round(x * sc);
    const float baseline = std::round(y * sc) + static_cast<float>(a.ascent);
    const float tracking = s.tracking * sc;
    r.beginGlyphs(a.tex, c);
    unsigned int prev = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned int cp = nextCodepoint(text, i);
        const Glyph& g = d_->glyph(a, cp);
        if (prev) {
            auto k = a.kerning.find({prev, cp});
            if (k != a.kerning.end()) pen += static_cast<float>(k->second);
        }
        if (g.w > 0) {
            const float gx = std::round(pen) + g.bx;
            const float gy = baseline - g.by;
            r.glyph(gx, gy, gx + g.w, gy + g.h, g.u0, g.v0, g.u1, g.v1);
        }
        pen += g.advance + tracking;
        prev = cp;
    }
    r.endGlyphs();
    return w;
}

std::vector<std::string> Fonts::wrap(const TextStyle& s, std::string_view text, float maxW) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && width(s, candidate) > maxW) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    };
    for (char ch : text) {
        if (ch == ' ') flushWord();
        else if (ch == '\n') { flushWord(); lines.push_back(line); line.clear(); }
        else word += ch;
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

float Fonts::drawWrapped(Renderer& r, const TextStyle& s, float x, float y, float w, std::string_view text, Color c,
                         float lineGap, int maxLines) {
    const auto lines = wrap(s, text, w);
    const float lh = lineHeight(s) + lineGap;
    float yy = y;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (maxLines > 0 && static_cast<int>(i) == maxLines - 1 && lines.size() > static_cast<std::size_t>(maxLines)) {
            draw(r, s, x, yy, lines[i] + " \xE2\x80\xA6", c, Align::Left, w);
            yy += lh;
            break;
        }
        draw(r, s, x, yy, lines[i], c);
        yy += lh;
    }
    return yy - y;
}

}  // namespace vigil::ui
