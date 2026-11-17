#pragma once
// A tiny anti-aliased renderer. Scenes are plain functions that call these shape methods in
// painter's order; smoothDraw() runs the scene once per 40-row strip into a 19 KB buffer and
// pushes each strip to the screen, so a full 115 KB frame never has to fit in RAM.
#include <Arduino_GFX.h>

struct Smooth {
  static const int W = 240, STRIP = 40;
  uint16_t buf[W * STRIP];
  int y0 = 0;            // first screen row held in buf
  float clipR = 1e9f;    // optional circular clip around the screen centre

  static float cover(float d) { return d <= -0.5f ? 1 : d >= 0.5f ? 0 : 0.5f - d; }

  static uint16_t mix(uint16_t a, uint16_t b, float t) {  // t = 0 -> a, 1 -> b
    int k = t * 32;
    int r = ((a >> 11) * (32 - k) + (b >> 11) * k) >> 5;
    int g = (((a >> 5) & 63) * (32 - k) + ((b >> 5) & 63) * k) >> 5;
    int bl = ((a & 31) * (32 - k) + (b & 31) * k) >> 5;
    return r << 11 | g << 5 | bl;
  }

  void plot(int x, int y, float a, uint16_t c) {
    if (a <= 0 || x < 0 || x >= W || y < y0 || y >= y0 + STRIP) return;
    if (clipR < 1e8f) a *= cover(hypotf(x + 0.5f - 120, y + 0.5f - 120) - clipR);
    uint16_t &p = buf[(y - y0) * W + x];
    p = a >= 1 ? c : mix(p, c, a);
  }

  // Rows of the current strip that overlap [top, bottom).
  int rowLo(float top) const { return max(y0, (int)floorf(top)); }
  int rowHi(float bottom) const { return min(y0 + STRIP, (int)ceilf(bottom)); }

  void fill(uint16_t c) {
    for (int i = 0; i < W * STRIP; i++) buf[i] = c;
  }

  // Filled circle; with c1 != c0 it is shaded diagonally from c0 (top-left) to c1 (bottom-right).
  void circle(float cx, float cy, float r, uint16_t c0, uint16_t c1) {
    for (int y = rowLo(cy - r - 1); y < rowHi(cy + r + 1); y++) {
      float dy = y + 0.5f - cy;
      if (fabsf(dy) > r + 1) continue;
      float in = r - 1 > fabsf(dy) ? sqrtf((r - 1) * (r - 1) - dy * dy) : -1;  // solid span half-width
      float out = sqrtf(max(0.0f, (r + 1) * (r + 1) - dy * dy));
      for (int x = floorf(cx - out); x <= ceilf(cx + out); x++) {
        float dx = x + 0.5f - cx;
        float a = fabsf(dx) < in ? 1 : cover(hypotf(dx, dy) - r);
        uint16_t c = c0 == c1 ? c0 : mix(c0, c1, constrain((dx + dy) / (2.8f * r) + 0.5f, 0.0f, 1.0f));
        plot(x, y, a, c);
      }
    }
  }
  void circle(float cx, float cy, float r, uint16_t c) { circle(cx, cy, r, c, c); }

  // Convex polygon from n (x, y) pairs, either winding.
  void poly(const float *p, int n, uint16_t c) {
    float area = 0, lo = 1e9f, hi = -1e9f, l = 1e9f, rr = -1e9f;
    for (int i = 0; i < n; i++) {
      int j = (i + 1) % n;
      area += p[2 * i] * p[2 * j + 1] - p[2 * j] * p[2 * i + 1];
      lo = min(lo, p[2 * i + 1]), hi = max(hi, p[2 * i + 1]), l = min(l, p[2 * i]), rr = max(rr, p[2 * i]);
    }
    float s = area > 0 ? 1 : -1;
    for (int y = rowLo(lo - 1); y < rowHi(hi + 1); y++)
      for (int x = floorf(l - 1); x <= ceilf(rr + 1); x++) {
        float d = -1e9f;
        for (int i = 0; i < n; i++) {
          int j = (i + 1) % n;
          float ex = p[2 * j] - p[2 * i], ey = p[2 * j + 1] - p[2 * i + 1], len = hypotf(ex, ey);
          d = max(d, s * ((x + 0.5f - p[2 * i]) * ey - (y + 0.5f - p[2 * i + 1]) * ex) / len);
        }
        plot(x, y, cover(d), c);
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
    float s = sinf(deg * DEG_TO_RAD), co = cosf(deg * DEG_TO_RAD), R = max(rx, ry) + 1;
    for (int y = rowLo(cy - R); y < rowHi(cy + R); y++)
      for (int x = floorf(cx - R); x <= ceilf(cx + R); x++) {
        float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
        float u = (dx * co + dy * s) / rx, v = (-dx * s + dy * co) / ry;
        plot(x, y, cover((sqrtf(u * u + v * v) - 1) * min(rx, ry)), c);
      }
  }

  // Ring stroke of the given width between angles a0..a1 (degrees in -180..180, 0 = right, 90 = down).
  void arc(float cx, float cy, float r, float width, float a0, float a1, uint16_t c) {
    float R = r + width;
    for (int y = rowLo(cy - R); y < rowHi(cy + R); y++)
      for (int x = floorf(cx - R); x <= ceilf(cx + R); x++) {
        float dx = x + 0.5f - cx, dy = y + 0.5f - cy, a = atan2f(dy, dx) * RAD_TO_DEG;
        if (a < a0 || a > a1) continue;
        plot(x, y, cover(fabsf(hypotf(dx, dy) - r) - width / 2), c);
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
    float R = hypotf(g.width + fabsf(g.xOffset - ax), g.height + fabsf(g.yOffset - ay)) * scale;
    for (int y = rowLo(cy - R); y < rowHi(cy + R); y++)
      for (int x = floorf(cx - R); x <= ceilf(cx + R); x++) {
        int hits = 0;
        for (int sy = 0; sy < 4; sy++)
          for (int sx = 0; sx < 4; sx++) {
            float dx = x + (sx + 0.5f) / 4 - cx, dy = y + (sy + 0.5f) / 4 - cy;
            int u = floorf((dx * co + dy * s) / scale + ax - g.xOffset);
            int v = floorf((-dx * s + dy * co) / scale + ay - g.yOffset);
            if (u < 0 || v < 0 || u >= g.width || v >= g.height) continue;
            int bit = v * g.width + u;
            hits += (f->bitmap[g.bitmapOffset + (bit >> 3)] >> (7 - (bit & 7))) & 1;
          }
        plot(x, y, hits / 16.0f, c);
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

  // Text along a circle of radius r around the screen centre, centred on angle deg, reading clockwise.
  void arcText(const GFXfont *f, float scale, const char *str, float r, float deg, float spacing, uint16_t c) {
    float len = 0;
    for (const char *p = str; *p; p++) len += advance(f, scale, *p) + spacing;
    float a = deg - (len - spacing) / 2 / r * RAD_TO_DEG;
    for (; *str; str++) {
      float half = advance(f, scale, *str) / 2 / r * RAD_TO_DEG;
      a += half;
      glyph(f, scale, *str, 120 + r * cosf(a * DEG_TO_RAD), 120 + r * sinf(a * DEG_TO_RAD), a + 90, c);
      a += half + spacing / r * RAD_TO_DEG;
    }
  }
};

typedef void (*SceneFn)(Smooth &s);

inline void smoothDraw(Arduino_GFX *gfx, SceneFn scene) {
  static Smooth s;  // 19 KB, static so it isn't on the stack
  for (s.y0 = 0; s.y0 < 240; s.y0 += Smooth::STRIP) {
    s.clipR = 1e9f;
    scene(s);
    gfx->draw16bitRGBBitmap(0, s.y0, s.buf, Smooth::W, Smooth::STRIP);
  }
}
