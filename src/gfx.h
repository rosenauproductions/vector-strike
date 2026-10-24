// Vector Strike - vector graphics: glow lines, polygons, stroke font (shared with Belt Command).
#pragma once
#include <SDL.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

static const float PI = 3.14159265358979f;

struct V2 { float x = 0, y = 0; V2() {} V2(float a, float b) : x(a), y(b) {} };
inline V2 operator+(V2 a, V2 b) { return V2(a.x + b.x, a.y + b.y); }
inline V2 operator-(V2 a, V2 b) { return V2(a.x - b.x, a.y - b.y); }
inline V2 operator-(V2 a) { return V2(-a.x, -a.y); }
inline V2 operator*(V2 a, float s) { return V2(a.x * s, a.y * s); }
inline V2 operator/(V2 a, float s) { return V2(a.x / s, a.y / s); }
inline V2& operator+=(V2& a, V2 b) { a.x += b.x; a.y += b.y; return a; }
inline V2& operator-=(V2& a, V2 b) { a.x -= b.x; a.y -= b.y; return a; }
inline V2& operator*=(V2& a, float s) { a.x *= s; a.y *= s; return a; }
inline float dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
inline float len2(V2 a) { return a.x * a.x + a.y * a.y; }
inline float len(V2 a) { return std::sqrt(len2(a)); }
inline float dist(V2 a, V2 b) { return len(a - b); }
inline V2 norm(V2 a) { float l = len(a); return l > 1e-6f ? a / l : V2(1, 0); }
inline V2 perp(V2 a) { return V2(-a.y, a.x); }
inline V2 fromAng(float a) { return V2(std::cos(a), std::sin(a)); }
inline V2 rotv(V2 v, float a) { float c = std::cos(a), s = std::sin(a); return V2(v.x * c - v.y * s, v.x * s + v.y * c); }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

struct Rng {
    uint32_t s = 0x12345678;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * f(); }
    int i(int n) { return n > 0 ? (int)(next() % (uint32_t)n) : 0; }
};

struct Col { uint8_t r = 255, g = 255, b = 255, a = 255; Col() {} Col(int R, int G, int B, int A = 255) : r((uint8_t)R), g((uint8_t)G), b((uint8_t)B), a((uint8_t)A) {} };
inline Col withA(Col c, float a) { c.a = (uint8_t)clampf(c.a * a, 0, 255); return c; }
inline Col mixc(Col a, Col b, float t) { return Col((int)lerpf(a.r, b.r, t), (int)lerpf(a.g, b.g, t), (int)lerpf(a.b, b.b, t), (int)lerpf(a.a, b.a, t)); }

// Collects triangles in two batches: alpha-blended fills and additive glowing lines.
struct Canvas {
    std::vector<SDL_Vertex> fillV, addV;
    bool glow = true;
    float px = 1;
    static SDL_Vertex mk(V2 p, Col c) { SDL_Vertex v; v.position.x = p.x; v.position.y = p.y; v.color.r = c.r; v.color.g = c.g; v.color.b = c.b; v.color.a = c.a; v.tex_coord.x = 0; v.tex_coord.y = 0; return v; }
    void tri(std::vector<SDL_Vertex>& b, V2 a, V2 c1, V2 c2, Col ca, Col cb, Col cc) { b.push_back(mk(a, ca)); b.push_back(mk(c1, cb)); b.push_back(mk(c2, cc)); }
    void quad(std::vector<SDL_Vertex>& b, V2 a, V2 c1, V2 c2, V2 d, Col ca, Col cb, Col cc, Col cd) { tri(b, a, c1, c2, ca, cb, cc); tri(b, a, c2, d, ca, cc, cd); }

    void line(V2 a, V2 b, float w, Col c, float glowMul = 1) {
        V2 d = b - a; float L = len(d); d = L > 1e-4f ? d / L : V2(1, 0);
        V2 n = perp(d);
        float hw = std::max(w * 0.5f, 0.45f);
        V2 a2 = a - d * (hw * 0.5f), b2 = b + d * (hw * 0.5f);
        quad(addV, a2 + n * hw, b2 + n * hw, b2 - n * hw, a2 - n * hw, c, c, c, c);
        if (glow && glowMul > 0) {
            float gw = hw * 2.6f + 3.0f * px;
            Col g = withA(c, 0.32f * glowMul), z = withA(c, 0);
            V2 ag = a - d * (gw * 0.5f), bg = b + d * (gw * 0.5f);
            quad(addV, a2 + n * hw, b2 + n * hw, bg + n * (hw + gw), ag + n * (hw + gw), g, g, z, z);
            quad(addV, a2 - n * hw, b2 - n * hw, bg - n * (hw + gw), ag - n * (hw + gw), g, g, z, z);
        }
    }
    void poly(const V2* p, int n, float w, Col c, float glowMul = 1, bool closed = true) {
        for (int i = 0; i < n - (closed ? 0 : 1); i++) line(p[i], p[(i + 1) % n], w, c, glowMul);
    }
    void fillPoly(const V2* p, int n, Col c) {
        V2 cen; for (int i = 0; i < n; i++) cen += p[i]; cen = cen / (float)n;
        for (int i = 0; i < n; i++) tri(fillV, cen, p[i], p[(i + 1) % n], c, c, c);
    }
    static int segsFor(float r) { return (int)clampf(r * 0.45f + 10, 10, 72); }
    void circle(V2 c, float r, float w, Col col, float glowMul = 1, int seg = 0) {
        if (r < 0.5f) return;
        if (!seg) seg = segsFor(r);
        V2 prev = c + V2(r, 0);
        for (int i = 1; i <= seg; i++) { V2 q = c + fromAng(i * 2 * PI / seg) * r; line(prev, q, w, col, glowMul); prev = q; }
    }
    void glowDisc(V2 c, float r, Col center) {
        int seg = std::max(12, segsFor(r) / 2); Col z = withA(center, 0);
        for (int i = 0; i < seg; i++) tri(addV, c, c + fromAng(i * 2 * PI / seg) * r, c + fromAng((i + 1) * 2 * PI / seg) * r, center, z, z);
    }
    void rect(float x, float y, float w, float h, Col c) { quad(fillV, V2(x, y), V2(x + w, y), V2(x + w, y + h), V2(x, y + h), c, c, c, c); }
    void rectAdd(float x, float y, float w, float h, Col c) { quad(addV, V2(x, y), V2(x + w, y), V2(x + w, y + h), V2(x, y + h), c, c, c, c); }
    void rectLine(float x, float y, float w, float h, float lw, Col c, float glowMul = 1) {
        V2 p[4] = { V2(x, y), V2(x + w, y), V2(x + w, y + h), V2(x, y + h) }; poly(p, 4, lw, c, glowMul);
    }
    void flush(SDL_Renderer* r) {
        if (!fillV.empty()) { SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND); SDL_RenderGeometry(r, nullptr, fillV.data(), (int)fillV.size(), nullptr, 0); }
        if (!addV.empty()) { SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD); SDL_RenderGeometry(r, nullptr, addV.data(), (int)addV.size(), nullptr, 0); }
        fillV.clear(); addV.clear();
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    }
};

// ---------- Stroke font: glyphs on a 4x6 grid, points as digit pairs, strokes split by spaces ----------
struct Font {
    std::vector<std::vector<V2>> strokes[128];
    void def(char ch, const char* s) {
        auto& g = strokes[(int)ch]; g.clear(); std::vector<V2> cur;
        for (const char* p = s; ; p++) {
            if (*p == ' ' || *p == 0) { if (cur.size() == 1) cur.push_back(cur[0] + V2(0, 0.2f)); if (!cur.empty()) g.push_back(cur); cur.clear(); if (!*p) break; continue; }
            if (!p[1]) break;
            cur.push_back(V2((float)(p[0] - '0'), (float)(p[1] - '0'))); p++;
        }
    }
    Font() {
        def('A', "0602204246 0343"); def('B', "06003041423303 3344453606"); def('C', "4130100105163645");
        def('D', "00304145360600"); def('E', "40000646 0333"); def('F', "400006 0333");
        def('G', "41301001051636454323"); def('H', "0006 4046 0343"); def('I', "1030 2026 1636");
        def('J', "4045361605"); def('K', "0006 400346"); def('L', "000646"); def('M', "0600234046");
        def('N', "06004640"); def('O', "103041453616050110"); def('P', "06003041423303");
        def('Q', "103041453616050110 2446"); def('R', "06003041423303 2346"); def('S', "413010010213334445361605");
        def('T', "0040 2026"); def('U', "000516364540"); def('V', "002640"); def('W', "0016233640");
        def('X', "0046 4006"); def('Y', "002340 2326"); def('Z', "00400646");
        def('0', "103041453616050110 4105"); def('1', "1120 2026 1636"); def('2', "01103041420646");
        def('3', "01103041423313 334445361605"); def('4', "36300444"); def('5', "4000033344453606");
        def('6', "30100105163645443303"); def('7', "004016"); def('8', "13020110304142331304051636454433");
        def('9', "43130201103041453616"); def('.', "2526"); def(',', "2517"); def(':', "2122 2526");
        def('-', "0343"); def('+', "0343 2125"); def('/', "0640"); def('!', "2024 2526");
        def('?', "01103041422324 2526"); def('%', "0640 0001 4546"); def('\'', "2021"); def('(', "30212536");
        def(')', "10212516"); def('>', "103316"); def('<', "301336"); def('=', "0242 0444"); def('*', "1135 3115 0343");
        def('[', "30101636"); def(']', "10303616"); def('_', "0646"); def('#', "1016 3036 0242 0444");
    }
    static float width(const char* s, float size) { int n = (int)strlen(s); return n ? n * size - size / 3 : 0; }
    float draw(Canvas& cv, const char* s, V2 pos, float size, Col c, int align = 0, float lw = 0, float glowMul = 0.7f) const {
        float w = width(s, size), u = size / 6.0f;
        if (!lw) lw = std::max(1.0f, size * 0.085f);
        float x = pos.x - (align == 1 ? w * 0.5f : align == 2 ? w : 0);
        for (const char* p = s; *p; p++) {
            int ch = (unsigned char)*p; if (ch >= 'a' && ch <= 'z') ch -= 32; if (ch >= 128) ch = '?';
            for (auto& st : strokes[ch]) for (size_t i = 0; i + 1 < st.size(); i++)
                cv.line(V2(x + st[i].x * u, pos.y + st[i].y * u), V2(x + st[i + 1].x * u, pos.y + st[i + 1].y * u), lw, c, glowMul);
            x += size;
        }
        return w;
    }
};
