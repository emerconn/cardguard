#pragma once
// A tiny anti-aliased renderer. Scenes are plain functions that call these shape methods in
// painter's order; smoothDraw() runs the scene once per 40-row strip into a 19 KB buffer and
// pushes each strip to the screen, so a full 115 KB frame never has to fit in RAM.
//
// The ESP32-C3 has no FPU, so per-pixel work is integer: float math happens once per shape or
// per row, inner loops step 16.16 fixed-point values, and sqrt only runs on edge pixels.
#include <Arduino_GFX.h>

struct Smooth {
  static const int W = 240, STRIP = 40;
  uint16_t buf[W * STRIP];
  int y0 = 0;                    // first screen row held in buf
  int32_t clipIn = 0, clipOut = 0;  // optional circular clip, squared radii in doubled pixel units
  float clipR = 0;

  static int32_t fx(float v) { return (int32_t)lroundf(v * 65536); }
  // Coverage 0..32 from signed distance to an edge (negative = inside).
  static int cover(float d) { return d <= -0.5f ? 32 : d >= 0.5f ? 0 : (int)((0.5f - d) * 32); }
  static int coverFx(int32_t d) { return d <= -32768 ? 32 : d >= 32768 ? 0 : (32768 - d) >> 11; }

  static uint16_t mix(uint16_t a, uint16_t b, int k) {  // k = 0 -> a, 32 -> b
    int r = ((a >> 11) * (32 - k) + (b >> 11) * k) >> 5;
    int g = (((a >> 5) & 63) * (32 - k) + ((b >> 5) & 63) * k) >> 5;
    int bl = ((a & 31) * (32 - k) + (b & 31) * k) >> 5;
    return r << 11 | g << 5 | bl;
  }

  // Clip later shapes to a circle of radius r around the screen centre; r = 0 turns it off.
  void clip(float r) {
    clipR = r;
    clipIn = r ? (int32_t)((2 * r - 1) * (2 * r - 1)) : 0;
    clipOut = r ? (int32_t)((2 * r + 1) * (2 * r + 1)) : 0;
  }

  void plot(int x, int y, int k, uint16_t c) {
    if (k <= 0 || x < 0 || x >= W || y < y0 || y >= y0 + STRIP) return;
    if (clipOut) {  // int test: a float compare here is a soft-float call on every pixel
      int32_t X = 2 * x + 1 - 240, Y = 2 * y + 1 - 240, D = X * X + Y * Y;
      if (D >= clipOut) return;
      if (D > clipIn) k = k * cover((D / 4.0f - clipR * clipR) / (2 * clipR)) >> 5;
    }
    uint16_t &p = buf[(y - y0) * W + x];
    p = k >= 32 ? c : mix(p, c, k);
  }

  // Rows of the current strip that overlap [top, bottom).
  int rowLo(float top) const { return max(y0, (int)floorf(top)); }
  int rowHi(float bottom) const { return min(y0 + STRIP, (int)ceilf(bottom)); }

  void fill(uint16_t c) {
    for (int i = 0; i < W * STRIP; i++) buf[i] = c;
  }

  // Filled circle; with c1 != c0 it is shaded diagonally from c0 (top-left) to c1 (bottom-right).
  // hole > 0 skips the middle (radius hole - 1) when a later shape will cover it anyway.
  void circle(float cx, float cy, float r, uint16_t c0, uint16_t c1, float hole = 0) {
    float g = 32 / (2.8f * r), inv2r = 0.5f / r;
    for (int y = rowLo(cy - r - 1); y < rowHi(cy + r + 1); y++) {
      float dy = y + 0.5f - cy;
      if (fabsf(dy) >= r + 0.5f) continue;
      float out = sqrtf((r + 0.5f) * (r + 0.5f) - dy * dy);
      float in = r - 0.5f > fabsf(dy) ? sqrtf((r - 0.5f) * (r - 0.5f) - dy * dy) : -1;
      int xa = ceilf(cx - out - 0.5f), xb = floorf(cx + out - 0.5f);
      int sa = in < 0 ? 1 : ceilf(cx - in - 0.5f), sb = in < 0 ? 0 : floorf(cx + in - 0.5f);
      float hs = hole - 1 > fabsf(dy) ? sqrtf((hole - 1) * (hole - 1) - dy * dy) : -1;
      int ha = hs < 0 ? -1 : ceilf(cx - hs - 0.5f), hb = hs < 0 ? -2 : floorf(cx + hs - 0.5f);
      int32_t kf = fx((xa + 0.5f - cx + dy) * g + 16), step = fx(g);
      for (int x = xa; x <= xb; x++, kf += step) {
        if (x == ha && ha <= hb) {  // jump over the hole
          kf += step * (hb - ha), x = hb;
          continue;
        }
        int k = 32;
        if (x < sa || x > sb) {  // edge pixel: |p| - r ~= (|p|^2 - r^2) / 2r, no sqrt needed
          float dx = x + 0.5f - cx;
          k = cover((dx * dx + dy * dy - r * r) * inv2r);
        }
        plot(x, y, k, c0 == c1 ? c0 : mix(c0, c1, constrain(kf >> 16, 0, 32)));
      }
    }
  }
  void circle(float cx, float cy, float r, uint16_t c) { circle(cx, cy, r, c, c); }

  // Convex polygon from n (x, y) pairs (n <= 8), either winding.
  void poly(const float *p, int n, uint16_t c) {
    float area = 0, lo = 1e9f, hi = -1e9f, l = 1e9f, rr = -1e9f;
    for (int i = 0; i < n; i++) {
      int j = (i + 1) % n;
      area += p[2 * i] * p[2 * j + 1] - p[2 * j] * p[2 * i + 1];
      lo = min(lo, p[2 * i + 1]), hi = max(hi, p[2 * i + 1]), l = min(l, p[2 * i]), rr = max(rr, p[2 * i]);
    }
    float s = area > 0 ? 1 : -1, nx[8], ny[8], nc[8];
    for (int i = 0; i < n; i++) {  // outward unit normals: distance d = nx*x + ny*y + nc
      int j = (i + 1) % n;
      float ex = p[2 * j] - p[2 * i], ey = p[2 * j + 1] - p[2 * i + 1], len = hypotf(ex, ey);
      nx[i] = s * ey / len, ny[i] = -s * ex / len, nc[i] = -(nx[i] * p[2 * i] + ny[i] * p[2 * i + 1]);
    }
    int xa = floorf(l - 1), xb = ceilf(rr + 1);
    int32_t d[8], step[8];
    for (int i = 0; i < n; i++) step[i] = fx(nx[i]);
    for (int y = rowLo(lo - 1); y < rowHi(hi + 1); y++) {
      for (int i = 0; i < n; i++) d[i] = fx(nx[i] * (xa + 0.5f) + ny[i] * (y + 0.5f) + nc[i]);
      for (int x = xa; x <= xb; x++) {
        int32_t m = d[0];
        for (int i = 0; i < n; i++) m = max(m, d[i]), d[i] += step[i];
        plot(x, y, coverFx(m), c);
      }
    }
  }
  void tri(float ax, float ay, float bx, float by, float cx, float cy, uint16_t c) {
    float p[] = {ax, ay, bx, by, cx, cy};
    poly(p, 3, c);
  }
  void rect(float x, float y, float w, float h, uint16_t c) {
    float p[] = {x, y, x + w, y, x + w, y + h, x, y + h};
    poly(p, 4, c);
  }

  // Ellipse rotated by deg (approximate distance, fine for small shapes like leaves).
  void ellipse(float cx, float cy, float rx, float ry, float deg, uint16_t c) {
    float s = sinf(deg * DEG_TO_RAD), co = cosf(deg * DEG_TO_RAD), R = max(rx, ry) + 1, m = min(rx, ry);
    int64_t qin = (int64_t)fx((1 - 0.5f / m) * (1 - 0.5f / m)), qout = (int64_t)fx((1 + 0.5f / m) * (1 + 0.5f / m));
    int32_t dux = fx(co / rx), dvx = fx(-s / ry);
    int xa = floorf(cx - R), xb = ceilf(cx + R);
    for (int y = rowLo(cy - R); y < rowHi(cy + R); y++) {
      float dx = xa + 0.5f - cx, dy = y + 0.5f - cy;
      int32_t u = fx((dx * co + dy * s) / rx), v = fx((-dx * s + dy * co) / ry);
      for (int x = xa; x <= xb; x++, u += dux, v += dvx) {
        int64_t q = ((int64_t)u * u + (int64_t)v * v) >> 16;
        if (q >= qout) continue;
        plot(x, y, q <= qin ? 32 : cover((sqrtf(q / 65536.0f) - 1) * m), c);
      }
    }
  }

  // Ring stroke of the given width between angles a0 < a1, less than 180 degrees apart
  // (degrees, 0 = right, 90 = down).
  void arc(float cx, float cy, float r, float width, float a0, float a1, uint16_t c) {
    float lo = 1e9f, hi = -1e9f, l = 1e9f, rr = -1e9f, R = r + width / 2 + 1;
    for (float a = a0;; a = min(a + 5, a1)) {  // bounding box of the swept band
      for (float rad = R - width - 2; rad <= R; rad += width + 2) {
        float x = cx + rad * cosf(a * DEG_TO_RAD), y = cy + rad * sinf(a * DEG_TO_RAD);
        lo = min(lo, y), hi = max(hi, y), l = min(l, x), rr = max(rr, x);
      }
      if (a >= a1) break;
    }
    float s0 = sinf(a0 * DEG_TO_RAD), c0 = cosf(a0 * DEG_TO_RAD), s1 = sinf(a1 * DEG_TO_RAD), c1 = cosf(a1 * DEG_TO_RAD);
    // Squared-distance limits of the band in doubled pixel units, so most of the box is rejected with ints.
    int32_t in = max(0.0f, 2 * (r - width / 2) - 1), out = 2 * (r + width / 2) + 1, cx2 = lroundf(2 * cx), cy2 = lroundf(2 * cy);
    int32_t fin = 2 * (r - width / 2) + 1, fout = max(0.0f, 2 * (r + width / 2) - 1);  // fully inside between these
    in *= in, out *= out, fin *= fin, fout *= fout;
    // Angle limits as 12-bit fixed-point directions, so the side test is integer too.
    int32_t S0 = s0 * 4096, C0 = c0 * 4096, S1 = s1 * 4096, C1 = c1 * 4096;
    float ri = r - width / 2, ro = r + width / 2;
    for (int y = rowLo(lo); y < rowHi(hi); y++)
      for (int x = floorf(l); x <= ceilf(rr); x++) {
        int32_t X = 2 * x + 1 - cx2, Y = 2 * y + 1 - cy2, D = X * X + Y * Y;
        if (D <= in || D >= out) continue;
        if (C0 * Y - S0 * X < 0 || X * S1 - Y * C1 < 0) continue;  // outside a0..a1
        if (D > fin && D < fout) {
          plot(x, y, 32, c);
          continue;
        }
        // Edge pixels only: distance to each edge radius e is ~= (|p|^2 - e^2) / 2e near that edge.
        float q = D / 4.0f;
        plot(x, y, cover(max((q - ro * ro) / (2 * ro), ri > 0 ? (ri * ri - q) / (2 * ri) : -1e9f)), c);
      }
  }

  // One glyph from a GFXfont drawn at `scale`, its centre at (cx, cy), rotated by deg.
  // Fonts are stored large (4x) and supersampled 4x4 here, which gives smooth edges.
  void glyph(const GFXfont *f, float scale, char ch, float cx, float cy, float deg, uint16_t c) {
    const GFXglyph &g = f->glyph[ch - f->first];
    if (!g.width) return;
    int top = 0;  // tallest glyph's top, i.e. cap height for a capitals-only font
    for (int i = 0; i <= f->last - f->first; i++) top = min(top, (int)f->glyph[i].yOffset);
    float ax = g.xAdvance / 2.0f, ay = top / 2.0f;  // anchor: advance centre, cap-height middle
    float s = sinf(deg * DEG_TO_RAD), co = cosf(deg * DEG_TO_RAD);

    float lo = 1e9f, hi = -1e9f, l = 1e9f, rr = -1e9f;  // screen box of the rotated glyph box
    for (int i = 0; i < 4; i++) {
      float u = (g.xOffset + (i & 1) * g.width - ax) * scale, v = (g.yOffset + (i >> 1) * g.height - ay) * scale;
      float x = cx + u * co - v * s, y = cy + u * s + v * co;
      lo = min(lo, y), hi = max(hi, y), l = min(l, x), rr = max(rr, x);
    }

    // Font-space position (u, v) of a screen point is linear in x and y; step it in fixed point.
    int32_t usx = fx(co / scale / 4), vsx = fx(-s / scale / 4), usy = fx(s / scale / 4), vsy = fx(co / scale / 4);
    const uint8_t *bits = f->bitmap + g.bitmapOffset;
    int xa = floorf(l), xb = ceilf(rr);
    for (int y = rowLo(lo); y < rowHi(hi); y++) {
      float dx = xa + 0.125f - cx, dy = y + 0.125f - cy;
      int32_t u = fx((dx * co + dy * s) / scale + ax - g.xOffset), v = fx((-dx * s + dy * co) / scale + ay - g.yOffset);
      for (int x = xa; x <= xb; x++, u += 4 * usx, v += 4 * vsx) {
        int hits = 0;
        int32_t uu = u, vv = v;
        for (int sy = 0; sy < 4; sy++, uu += usy - 4 * usx, vv += vsy - 4 * vsx)
          for (int sx = 0; sx < 4; sx++, uu += usx, vv += vsx) {
            uint32_t iu = uu >> 16, iv = vv >> 16;  // negative wraps to huge, failing the bounds test
            if (iu >= g.width || iv >= g.height) continue;
            int bit = iv * g.width + iu;
            hits += (bits[bit >> 3] >> (7 - (bit & 7))) & 1;
          }
        plot(x, y, hits * 2, c);
      }
    }
  }

  float advance(const GFXfont *f, float scale, char ch) { return f->glyph[ch - f->first].xAdvance * scale; }

  // Upright text centred on (cx, cy).
  void text(const GFXfont *f, float scale, const char *str, float cx, float cy, uint16_t c) {
    float w = 0;
    for (const char *p = str; *p; p++) w += advance(f, scale, *p);
    for (float x = cx - w / 2; *str; str++) {
      float a = advance(f, scale, *str);
      glyph(f, scale, *str, x + a / 2, cy, 0, c);
      x += a;
    }
  }

  // Text along a circle of radius r around the screen centre, centred on angle deg.
  // Top text reads clockwise; bottom = true reads counter-clockwise so it stays upright.
  void arcText(const GFXfont *f, float scale, const char *str, float r, float deg, float spacing, uint16_t c,
               bool bottom = false) {
    float len = 0, dir = bottom ? -1 : 1;
    for (const char *p = str; *p; p++) len += advance(f, scale, *p) + spacing;
    float a = deg - dir * (len - spacing) / 2 / r * RAD_TO_DEG;
    for (; *str; str++) {
      float half = dir * advance(f, scale, *str) / 2 / r * RAD_TO_DEG;
      a += half;
      glyph(f, scale, *str, 120 + r * cosf(a * DEG_TO_RAD), 120 + r * sinf(a * DEG_TO_RAD), a + 90 * dir, c);
      a += half + dir * spacing / r * RAD_TO_DEG;
    }
  }
};

typedef void (*SceneFn)(Smooth &s);

inline void smoothDraw(Arduino_GFX *gfx, SceneFn scene) {
  static Smooth s;  // 19 KB, static so it isn't on the stack
  for (s.y0 = 0; s.y0 < 240; s.y0 += Smooth::STRIP) {
    s.clip(0);
    scene(s);
    gfx->draw16bitRGBBitmap(0, s.y0, s.buf, Smooth::W, Smooth::STRIP);
  }
}
