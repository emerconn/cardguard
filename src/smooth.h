#pragma once
// A tiny anti-aliased renderer. Scenes are plain functions that call these shape methods in
// painter's order; smoothDraw() runs the scene once per 80-row strip into a 38 KB buffer and
// pushes each strip to the screen, so a full 115 KB frame never has to fit in RAM.
//
// The ESP32-C3 has no FPU, so per-pixel work is integer: float math happens once per shape or
// per row, inner loops step 16.16 fixed-point values, and sqrt only runs on edge pixels.
// The scene runs once per strip, so every shape first checks it touches the strip before any setup maths.
#include <Arduino_GFX.h>
#pragma GCC optimize("O2")  // ~20% faster than the framework's -Os for these pixel loops

struct Smooth {
  static const int W = 240, STRIP = 80;
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

  bool skip(float top, float bottom) const { return bottom < y0 || top >= y0 + STRIP; }

  static uint32_t isqrt(uint32_t v) {  // integer sqrt, far cheaper than soft-float sqrtf
    uint32_t r = 0;
    for (uint32_t b = 1u << 30; b; b >>= 2)
      if (v >= r + b) v -= r + b, r = (r >> 1) + b;
      else r >>= 1;
    return r;
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
    if (skip(cy - r - 1, cy + r + 1)) return;
    // Positions in 24.8 fixed point; their squares carry 16 fraction bits, so sqrt gives 8 again.
    int32_t cx8 = lroundf(cx * 256), cy8 = lroundf(cy * 256);
    int32_t out2 = lroundf((r + 0.5f) * (r + 0.5f) * 65536), in2 = r > 0.5f ? lroundf((r - 0.5f) * (r - 0.5f) * 65536) : -1;
    int32_t hole2 = hole > 1 ? lroundf((hole - 1) * (hole - 1) * 65536) : -1, r2 = lroundf(r * r * 65536);
    int64_t inv2r = lroundf(65536 / (2 * r));            // edge distance (|p|^2 - r^2) / 2r, in 16.16
    int64_t g = lroundf(32 / (2.8f * r) * 65536);        // gradient step per pixel, 16.16
    for (int y = rowLo(cy - r - 1); y < rowHi(cy + r + 1); y++) {
      int32_t dy8 = y * 256 + 128 - cy8, dy2 = dy8 * dy8;
      if (dy2 >= out2) continue;
      int32_t out = isqrt(out2 - dy2), in = dy2 < in2 ? isqrt(in2 - dy2) : -1, hs = dy2 < hole2 ? isqrt(hole2 - dy2) : -1;
      // Pixel x is inside a half-span h when |x*256 + 128 - cx8| <= h.
      int xa = (cx8 - out - 128 + 255) >> 8, xb = (cx8 + out - 128) >> 8;
      int sa = in < 0 ? 1 : (cx8 - in - 128 + 255) >> 8, sb = in < 0 ? 0 : (cx8 + in - 128) >> 8;
      int ha = hs < 0 ? -1 : (cx8 - hs - 128 + 255) >> 8, hb = hs < 0 ? -2 : (cx8 + hs - 128) >> 8;
      int32_t kf = (((int64_t)(xa * 256 + 128 - cx8 + dy8) * g) >> 8) + (16 << 16);
      for (int x = xa; x <= xb; x++, kf += g) {
        if (x == ha && ha <= hb) {  // jump over the hole
          kf += g * (hb - ha), x = hb;
          continue;
        }
        int k = 32;
        if (x < sa || x > sb) {  // edge pixel: |p| - r ~= (|p|^2 - r^2) / 2r, no sqrt needed
          int32_t dx8 = x * 256 + 128 - cx8;
          k = coverFx(((int64_t)(dx8 * dx8 + dy2 - r2) * inv2r) >> 16);
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
    if (skip(lo - 1, hi + 1)) return;
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
    float R = max(rx, ry) + 1;
    if (skip(cy - R, cy + R)) return;
    float s = sinf(deg * DEG_TO_RAD), co = cosf(deg * DEG_TO_RAD), m = min(rx, ry);
    int64_t mFx = fx(m);
    int64_t qin = (int64_t)fx((1 - 0.5f / m) * (1 - 0.5f / m)), qout = (int64_t)fx((1 + 0.5f / m) * (1 + 0.5f / m));
    int32_t dux = fx(co / rx), dvx = fx(-s / ry);
    int xa = floorf(cx - R), xb = ceilf(cx + R);
    for (int y = rowLo(cy - R); y < rowHi(cy + R); y++) {
      float dx = xa + 0.5f - cx, dy = y + 0.5f - cy;
      int32_t u = fx((dx * co + dy * s) / rx), v = fx((-dx * s + dy * co) / ry);
      for (int x = xa; x <= xb; x++, u += dux, v += dvx) {
        int64_t q = ((int64_t)u * u + (int64_t)v * v) >> 16;
        if (q >= qout) continue;
        if (q <= qin) {
          plot(x, y, 32, c);
          continue;
        }
        int64_t e = q - 65536, root = (e >> 1) - ((e * e) >> 19);  // sqrt(1 + e) - 1 ~= e/2 - e^2/8, 16.16
        plot(x, y, coverFx((root * mFx) >> 16), c);
      }
    }
  }

  // Rounded rectangle w x h with corner radius rad, centred on (cx, cy), rotated by deg.
  void rrect(float cx, float cy, float w, float h, float rad, float deg, uint16_t c) {
    if (skip(cy - (w + h) / 2 - 1, cy + (w + h) / 2 + 1)) return;  // loose bound before any trig
    float s = sinf(deg * DEG_TO_RAD), co = cosf(deg * DEG_TO_RAD);
    float Rx = fabsf(w / 2 * co) + fabsf(h / 2 * s) + 1, Ry = fabsf(w / 2 * s) + fabsf(h / 2 * co) + 1;
    int32_t ex = fx(w / 2 - rad), ey = fx(h / 2 - rad), fr = fx(rad);  // straight-edge half extents
    int32_t dux = fx(co), dvx = fx(-s);
    int xa = floorf(cx - Rx), xb = ceilf(cx + Rx);
    for (int y = rowLo(cy - Ry); y < rowHi(cy + Ry); y++) {
      float dx = xa + 0.5f - cx, dy = y + 0.5f - cy;
      int32_t u = fx(dx * co + dy * s), v = fx(-dx * s + dy * co);  // pixel in the rectangle's own axes
      for (int x = xa; x <= xb; x++, u += dux, v += dvx) {
        int32_t qx = abs(u) - ex, qy = abs(v) - ey;
        if (qx > fr + 65536 || qy > fr + 65536) continue;
        if (qx > 0 && qy > 0) {  // corner: distance to the rounding circle, integer sqrt in 8.8
          int32_t qx8 = qx >> 8, qy8 = qy >> 8;
          plot(x, y, coverFx(((int32_t)isqrt(qx8 * qx8 + qy8 * qy8) << 8) - fr), c);
        } else {
          plot(x, y, coverFx(max(qx, qy) - fr), c);
        }
      }
    }
  }

  // Ring stroke of the given width between angles a0 < a1, less than 180 degrees apart
  // (degrees, 0 = right, 90 = down).
  void arc(float cx, float cy, float r, float width, float a0, float a1, uint16_t c) {
    float R = r + width / 2 + 1, Ri = max(0.0f, r - width / 2 - 1);
    if (skip(cy - R, cy + R)) return;
    float s0 = sinf(a0 * DEG_TO_RAD), c0 = cosf(a0 * DEG_TO_RAD), s1 = sinf(a1 * DEG_TO_RAD), c1 = cosf(a1 * DEG_TO_RAD);
    // Bounding box: both ends at both radii, plus the outer extreme of any axis the arc crosses.
    float lo = cy + min(min(Ri * s0, R * s0), min(Ri * s1, R * s1)), hi = cy + max(max(Ri * s0, R * s0), max(Ri * s1, R * s1));
    float l = cx + min(min(Ri * c0, R * c0), min(Ri * c1, R * c1)), rr = cx + max(max(Ri * c0, R * c0), max(Ri * c1, R * c1));
    for (int q = -180; q <= 180; q += 90)
      if (q > a0 && q < a1) {
        if (q == 0) rr = cx + R;
        if (q == 90) hi = cy + R;
        if (q == -180 || q == 180) l = cx - R;
        if (q == -90) lo = cy - R;
      }
    if (skip(lo, hi)) return;
    // Squared-distance limits of the band in doubled pixel units, so most of the box is rejected with ints.
    int32_t in = max(0.0f, 2 * (r - width / 2) - 1), out = 2 * (r + width / 2) + 1, cx2 = lroundf(2 * cx), cy2 = lroundf(2 * cy);
    int32_t fin = 2 * (r - width / 2) + 1, fout = max(0.0f, 2 * (r + width / 2) - 1);  // fully inside between these
    in *= in, out *= out, fin *= fin, fout *= fout;
    // Angle limits as 12-bit fixed-point directions, so the side test is integer too.
    int32_t S0 = s0 * 4096, C0 = c0 * 4096, S1 = s1 * 4096, C1 = c1 * 4096;
    // Edge distance (|p|^2 - e^2) / 2e for each edge radius e, kept in ints: |p|^2 = D / 4.
    float ri = r - width / 2, ro = r + width / 2;
    int32_t ro4 = lroundf(4 * ro * ro), ri4 = lroundf(4 * ri * ri);
    int64_t kOut = llroundf(65536.0f * 65536.0f / (8 * ro)), kIn = ri > 0 ? llroundf(65536.0f * 65536.0f / (8 * ri)) : 0;
    for (int y = rowLo(lo); y < rowHi(hi); y++)
      for (int x = floorf(l); x <= ceilf(rr); x++) {
        int32_t X = 2 * x + 1 - cx2, Y = 2 * y + 1 - cy2, D = X * X + Y * Y;
        if (D <= in || D >= out) continue;
        // Distance inside each end ray, in 1/8192 px (doubled coords x 4096); feather the ends over a pixel.
        int32_t A0 = C0 * Y - S0 * X, A1 = X * S1 - Y * C1;
        if (A0 <= -4096 || A1 <= -4096) continue;  // over half a pixel outside a0..a1
        if (D > fin && D < fout && A0 >= 4096 && A1 >= 4096) {
          plot(x, y, 32, c);
          continue;
        }
        // Edge pixels only: distance to each edge radius e is ~= (|p|^2 - e^2) / 2e near that edge.
        int32_t dOut = ((int64_t)(D - ro4) * kOut) >> 16, dIn = ri > 0 ? ((int64_t)(ri4 - D) * kIn) >> 16 : INT32_MIN;
        int32_t dEnd = -8 * min(A0, A1);  // to 16.16 px, positive outside
        plot(x, y, coverFx(max(max(dOut, dIn), dEnd)), c);
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
    float Rb = (fabsf(g.xOffset - ax) + g.width + fabsf(g.yOffset - ay) + g.height) * scale;  // loose bound
    if (skip(cy - Rb, cy + Rb)) return;
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
  static Smooth s;  // 38 KB, static so it isn't on the stack
  for (s.y0 = 0; s.y0 < 240; s.y0 += Smooth::STRIP) {
    s.clip(0);
    scene(s);
    gfx->draw16bitRGBBitmap(0, s.y0, s.buf, Smooth::W, Smooth::STRIP);
  }
}
