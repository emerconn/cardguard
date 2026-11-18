#pragma once
// A tiny anti-aliased renderer. Scenes are plain functions that call these shape methods in
// painter's order; smoothDraw() runs the scene once per 120-row strip into a 58 KB buffer and
// pushes each strip to the screen, so a full 115 KB frame never has to fit in RAM.
//
// The ESP32-C3 has no FPU, so per-pixel work is integer: float math happens once per shape or
// per row, inner loops step 16.16 fixed-point values, and sqrt only runs on edge pixels.
// The scene runs once per strip, so every shape first checks it touches the strip before any setup maths.
#include <Arduino_GFX.h>
#pragma GCC optimize("O2")  // ~20% faster than the framework's -Os for these pixel loops

// sin/cos in integer maths: newlib's sinf/cosf cost ~40 us each here (~800 calls a frame).
// The angle becomes a 32-bit phase (one turn = 2^32), folded to a quadrant, then a Q30 Taylor
// polynomial up to x^11 / x^12; error ~1e-7, on par with float itself.
inline float sinPhase(uint32_t ph) {
  int64_t x = ((int64_t)(ph & 0x3FFFFFFF) * 1686629713) >> 30, x2 = (x * x) >> 30, t;  // pi/2 in Q30
  if (ph & 0x40000000) {  // 2nd/4th quadrant: cos of the angle within it
    t = 2;
    for (int32_t c : {-296, 26631, -1491308, 44739243, -536870912, 1 << 30}) t = c + ((x2 * t) >> 30);
  } else {
    t = -27;
    for (int32_t c : {2959, -213040, 8947849, -178956971, 1 << 30}) t = c + ((x2 * t) >> 30);
    t = (x * t) >> 30;
  }
  return (ph & 0x80000000 ? -t : t) * (1.0f / (1 << 30));
}
// rad * 2^24 is exact in float; the turn conversion (x 256 / 2pi, in Q26) stays in integers.
inline uint32_t radPhase(float rad) { return (uint32_t)((llroundf(rad * 16777216.0f) * 2734261102LL) >> 26); }
inline float fsin(float rad) { return sinPhase(radPhase(rad)); }
inline float fcos(float rad) { return sinPhase(radPhase(rad) + 0x40000000); }

struct Smooth {
  static const int W = 240, STRIP = 120;
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

  // Narrows a row's columns [a, b] to the screen and the clip circle, so loops skip pixels plot() would drop.
  void rowSpan(int y, int &a, int &b) const {
    a = max(a, 0), b = min(b, W - 1);
    if (!clipOut) return;
    int32_t Y = 2 * y + 1 - 240, r2 = clipOut - 1 - Y * Y;
    if (r2 < 0) return void(b = a - 1);
    int32_t m = isqrt(r2);  // |2x + 1 - 240| <= m keeps D < clipOut
    a = max(a, (int)(240 - m) >> 1), b = min(b, (int)(239 + m) >> 1);
  }

  static uint32_t isqrt(uint32_t v) {  // integer sqrt, far cheaper than soft-float sqrtf
    uint32_t r = 0;
    for (uint32_t b = 1u << 30; b; b >>= 2)
      if (v >= r + b) v -= r + b, r = (r >> 1) + b;
      else r >>= 1;
    return r;
  }

  // Narrows columns t in [lo, hi] to those where d + step * t <= lim (16.16 distances along a row).
  static void below(int32_t d, int32_t step, int32_t lim, int &lo, int &hi) {
    int32_t a = lim - d;
    if (step > 0) hi = min(hi, (int)(a >= 0 ? a / step : -((-a + step - 1) / step)));  // floor(a / step)
    else if (step < 0) lo = max(lo, (int)(a <= 0 ? (-a - step - 1) / -step : -(a / -step)));  // ceil(a / step)
    else if (a < 0) hi = lo - 1;
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
    uint16_t shade[33];                                    // the gradient's 33 levels, mixed once
    for (int k = 0; k <= 32 && c0 != c1; k++) shade[k] = mix(c0, c1, k);
    for (int y = rowLo(cy - r - 1); y < rowHi(cy + r + 1); y++) {
      int32_t dy8 = y * 256 + 128 - cy8, dy2 = dy8 * dy8;
      if (dy2 >= out2) continue;
      int32_t out = isqrt(out2 - dy2), in = dy2 < in2 ? isqrt(in2 - dy2) : -1, hs = dy2 < hole2 ? isqrt(hole2 - dy2) : -1;
      // Pixel x is inside a half-span h when |x*256 + 128 - cx8| <= h.
      int xa = (cx8 - out - 128 + 255) >> 8, xb = (cx8 + out - 128) >> 8;
      int sa = in < 0 ? 1 : (cx8 - in - 128 + 255) >> 8, sb = in < 0 ? 0 : (cx8 + in - 128) >> 8;
      int ha = hs < 0 ? -1 : (cx8 - hs - 128 + 255) >> 8, hb = hs < 0 ? -2 : (cx8 + hs - 128) >> 8;
      int lo = xa, hi = xb;
      rowSpan(y, lo, hi);
      int32_t kf = (((int64_t)(lo * 256 + 128 - cx8 + dy8) * g) >> 8) + (16 << 16);
      uint16_t *row = buf + (y - y0) * W;
      for (int x = lo; x <= hi; x++, kf += g) {
        if (x >= ha && x <= hb) {  // jump over the hole
          kf += g * (hb - x), x = hb;
          continue;
        }
        uint16_t c = c0 == c1 ? c0 : shade[constrain(kf >> 16, 0, 32)];
        if (x < sa || x > sb) {  // edge pixel: |p| - r ~= (|p|^2 - r^2) / 2r, no sqrt needed
          int32_t dx8 = x * 256 + 128 - cx8;
          plot(x, y, coverFx(((int64_t)(dx8 * dx8 + dy2 - r2) * inv2r) >> 16), c);
        } else if (clipOut) {
          plot(x, y, 32, c);
        } else {
          row[x] = c;  // fully covered, on screen and unclipped: plot() would only add checks
        }
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
    int32_t stepY[8], yLo = rowLo(lo - 1);
    for (int i = 0; i < n; i++) step[i] = fx(nx[i]), stepY[i] = fx(ny[i]), d[i] = fx(nx[i] * (xa + 0.5f) + ny[i] * (yLo + 0.5f) + nc[i]);
    for (int y = yLo; y < rowHi(hi + 1); y++) {
      // Distances are linear along the row (d + step * t, t = x - xa), so each edge bounds the
      // columns with any coverage (all d < half a pixel) and with full coverage (all d <= -half).
      int t0 = 0, t1 = xb - xa, f0 = 0, f1 = t1;
      for (int i = 0; i < n; i++) {
        if (y > yLo) d[i] += stepY[i];
        below(d[i], step[i], 32767, t0, t1);
        below(d[i], step[i], -32768, f0, f1);
      }
      int lo = xa + t0, hi = xa + t1;
      rowSpan(y, lo, hi);
      uint16_t *row = buf + (y - y0) * W;
      for (int x = lo; x <= hi; x++) {
        int t = x - xa;
        if (t >= f0 && t <= f1 && !clipOut) {
          for (int e = min(xa + f1, hi); x <= e; x++) row[x] = c;
          x--;
          continue;
        }
        int32_t m = d[0] + step[0] * t;
        for (int i = 1; i < n; i++) m = max(m, d[i] + step[i] * t);
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
    float s = fsin(deg * DEG_TO_RAD), co = fcos(deg * DEG_TO_RAD), m = min(rx, ry);
    int64_t mFx = fx(m);
    int64_t qin = (int64_t)fx((1 - 0.5f / m) * (1 - 0.5f / m)), qout = (int64_t)fx((1 + 0.5f / m) * (1 + 0.5f / m));
    int32_t dux = fx(co / rx), dvx = fx(-s / ry);
    // Row spans: q = 1 is the quadratic form a dx^2 + 2b dx dy + c dy^2 = 1, so a level-S curve meets a row
    // at xc = cx - (b/a) dy, half width^2 = S/a - (ac - b^2)/a^2 dy^2. Only a sqrt per row, in ints.
    float ix = 1 / (rx * rx), iy = 1 / (ry * ry), a = co * co * ix + s * s * iy, bb = co * s * (ix - iy);
    int32_t cx16 = fx(cx), cy16 = fx(cy), kb = fx(-bb / a), hOut = fx((qout / 65536.0f + 0.01f) / a);
    int32_t hIn = qin > 1000 ? fx((qin / 65536.0f - 0.01f) / a) : -1;  // padded inward: interior is a safe subset
    int64_t h1 = llroundf(ix * iy / (a * a) * 4294967296.0f);
    int xa = floorf(cx - R), xb = ceilf(cx + R);
    int yLo = rowLo(cy - R);
    float dx0 = xa + 0.5f - cx, dy0 = yLo + 0.5f - cy;  // row-start (u, v) at the first row, then stepped
    int32_t u0 = fx((dx0 * co + dy0 * s) / rx), v0 = fx((-dx0 * s + dy0 * co) / ry), duy = fx(s / rx), dvy = fx(co / ry);
    for (int y = yLo; y < rowHi(cy + R); y++, u0 += duy, v0 += dvy) {
      int32_t dy16 = (y << 16) + 32768 - cy16, xc = cx16 + (int32_t)(((int64_t)kb * dy16) >> 16) - 32768;
      int32_t sub = (((h1 * dy16) >> 32) * dy16) >> 16, wo = hOut - sub, wi = hIn - sub;
      if (wo < 0) continue;
      int32_t ho = isqrt(wo) << 8, hi2 = wi > 0 ? isqrt(wi) << 8 : -1;  // half widths, 16.16
      // Columns (pixel centre x + 0.5) inside each half width, a pixel of slack against rounding.
      int lo = max(xa, (int)((xc - ho) >> 16) - 1), hi = min(xb, (int)((xc + ho + 65535) >> 16) + 1);
      int ia = hi2 < 0 || clipOut ? 1 : (int)((xc - hi2 + 65535) >> 16) + 1, ib = hi2 < 0 || clipOut ? 0 : (int)((xc + hi2) >> 16) - 1;
      rowSpan(y, lo, hi);
      int32_t u = u0 + dux * (lo - xa), v = v0 + dvx * (lo - xa);
      uint16_t *row = buf + (y - y0) * W;
      for (int x = lo; x <= hi; x++, u += dux, v += dvx) {
        if (x >= ia && x <= ib) {  // interior: fill straight through
          int e = min(ib, hi);
          for (int i = x; i <= e; i++) row[i] = c;
          u += dux * (e - x), v += dvx * (e - x), x = e;
          continue;
        }
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
    float s = fsin(deg * DEG_TO_RAD), co = fcos(deg * DEG_TO_RAD);
    float Rx = fabsf(w / 2 * co) + fabsf(h / 2 * s) + 1, Ry = fabsf(w / 2 * s) + fabsf(h / 2 * co) + 1;
    int32_t ex = fx(w / 2 - rad), ey = fx(h / 2 - rad), fr = fx(rad);  // straight-edge half extents
    int32_t dux = fx(co), dvx = fx(-s);
    int xa = floorf(cx - Rx), xb = ceilf(cx + Rx);
    int yLo = rowLo(cy - Ry);
    float dx = xa + 0.5f - cx, dy = yLo + 0.5f - cy;
    int32_t u0 = fx(dx * co + dy * s), v0 = fx(-dx * s + dy * co), duy = fx(s), dvy = fx(co);  // rectangle's own axes
    for (int y = yLo; y < rowHi(cy + Ry); y++, u0 += duy, v0 += dvy) {
      int32_t u = u0, v = v0;
      // u and v are linear along the row: bound the columns that can be covered (|u|, |v| within the
      // corner reach) and those fully inside (straight-edge zone at least half a pixel in).
      int t0 = 0, t1 = xb - xa, f0 = 0, f1 = t1;
      int32_t ox = ex + fr + 65536, oy = ey + fr + 65536, in = min((int32_t)0, fr - 32768);
      below(u, dux, ox, t0, t1), below(-u, -dux, ox, t0, t1), below(v, dvx, oy, t0, t1), below(-v, -dvx, oy, t0, t1);
      below(u, dux, ex + in, f0, f1), below(-u, -dux, ex + in, f0, f1), below(v, dvx, ey + in, f0, f1), below(-v, -dvx, ey + in, f0, f1);
      int lo = xa + t0, hi = xa + t1;
      rowSpan(y, lo, hi);
      if (clipOut) f1 = f0 - 1;
      u += dux * (lo - xa), v += dvx * (lo - xa);
      uint16_t *row = buf + (y - y0) * W;
      for (int x = lo; x <= hi; x++, u += dux, v += dvx) {
        if (x - xa >= f0 && x - xa <= f1) {  // interior: fill straight through
          int e = min(xa + f1, hi);
          for (int i = x; i <= e; i++) row[i] = c;
          u += dux * (e - x), v += dvx * (e - x), x = e;
          continue;
        }
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
    float s0 = fsin(a0 * DEG_TO_RAD), c0 = fcos(a0 * DEG_TO_RAD), s1 = fsin(a1 * DEG_TO_RAD), c1 = fcos(a1 * DEG_TO_RAD);
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
    for (int y = rowLo(lo); y < rowHi(hi); y++) {
      int32_t Y = 2 * y + 1 - cy2, rest = out - 1 - Y * Y;
      if (rest < 0) continue;
      // Only columns between the outer and inner circle: |X| <= m keeps D < out, |X| <= h keeps D <= in.
      int32_t m = isqrt(rest), h = in - Y * Y >= 0 ? isqrt(in - Y * Y) : -1;
      int xa = max((int)floorf(l), (int)(cx2 - m) >> 1), xb = min((int)ceilf(rr), (int)(cx2 - 1 + m) >> 1);
      int ha = (cx2 - h) >> 1, hb = (cx2 - 1 + h) >> 1;  // hole columns; ha > hb when there is none
      rowSpan(y, xa, xb);
      for (int x = xa; x <= xb; x++) {
        if (x >= ha && x <= hb) {
          x = hb;
          continue;
        }
        int32_t X = 2 * x + 1 - cx2, D = X * X + Y * Y;
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
    float s = fsin(deg * DEG_TO_RAD), co = fcos(deg * DEG_TO_RAD);

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
    int yLo = rowLo(lo);
    float dx = xa + 0.125f - cx, dy = yLo + 0.125f - cy;
    int32_t u0 = fx((dx * co + dy * s) / scale + ax - g.xOffset), v0 = fx((-dx * s + dy * co) / scale + ay - g.yOffset);
    int32_t duy = fx(s / scale), dvy = fx(co / scale);  // row-start step
    for (int y = yLo; y < rowHi(hi); y++, u0 += duy, v0 += dvy) {
      int32_t u = u0, v = v0;
      // Skip columns whose 4x4 samples all miss the glyph box (rotated glyphs leave empty box corners).
      int t0 = 0, t1 = xb - xa;
      int32_t uMin = 3 * (min(usx, (int32_t)0) + min(usy, (int32_t)0)), uMax = 3 * (max(usx, (int32_t)0) + max(usy, (int32_t)0));
      int32_t vMin = 3 * (min(vsx, (int32_t)0) + min(vsy, (int32_t)0)), vMax = 3 * (max(vsx, (int32_t)0) + max(vsy, (int32_t)0));
      below(u + uMin, 4 * usx, (g.width << 16) - 1, t0, t1), below(-(u + uMax), -4 * usx, 0, t0, t1);
      below(v + vMin, 4 * vsx, (g.height << 16) - 1, t0, t1), below(-(v + vMax), -4 * vsx, 0, t0, t1);
      int lo = xa + t0, hi = xa + t1;
      rowSpan(y, lo, hi);
      u += 4 * usx * (lo - xa), v += 4 * vsx * (lo - xa);
      for (int x = lo; x <= hi; x++, u += 4 * usx, v += 4 * vsx) {
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
      glyph(f, scale, *str, 120 + r * fcos(a * DEG_TO_RAD), 120 + r * fsin(a * DEG_TO_RAD), a + 90 * dir, c);
      a += half + dir * spacing / r * RAD_TO_DEG;
    }
  }
};

typedef void (*SceneFn)(Smooth &s);

inline void smoothDraw(Arduino_GFX *gfx, SceneFn scene) {
  static Smooth s;  // 58 KB, static so it isn't on the stack
  for (s.y0 = 0; s.y0 < 240; s.y0 += Smooth::STRIP) {
    s.clip(0);
    scene(s);
    gfx->draw16bitRGBBitmap(0, s.y0, s.buf, Smooth::W, Smooth::STRIP);
  }
}
