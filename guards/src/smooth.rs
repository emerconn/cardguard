//! A tiny anti-aliased renderer. Scenes are plain functions that call these shape methods in
//! painter's order; the caller runs the scene once per 120-row strip into a 58 KB buffer and
//! pushes each strip to the screen, so a full 115 KB frame never has to fit in RAM.
//!
//! The ESP32-C3 has no FPU, so per-pixel work is integer: float math happens once per shape or
//! per row, inner loops step 16.16 fixed-point values, and sqrt only runs on edge pixels.
//! The scene runs once per strip, so every shape first checks it touches the strip before any setup maths.
//!
//! The buffer holds pixels in the panel's byte order (big-endian RGB565), so a strip goes to the
//! display as is; only blended edge pixels pay a byte swap.
use libm::{ceilf, fabsf, floorf, hypotf, roundf};

// Doubles, as Arduino defines them: angle conversions round once from double, like the C++ original did.
const DEG_TO_RAD: f64 = 0.017453292519943295769236907684886;
const RAD_TO_DEG: f64 = 57.295779513082320876798154814105;
/// Degrees to radians.
pub fn rad(deg: f32) -> f32 {
    (deg as f64 * DEG_TO_RAD) as f32
}

pub const fn rgb565(r: u16, g: u16, b: u16) -> u16 {
    ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
}

/// One glyph of a 1-bit font (Adafruit GFXfont layout).
pub struct Glyph {
    pub offset: u16,
    pub w: u8,
    pub h: u8,
    pub adv: u8,
    pub xo: i8,
    pub yo: i8,
}
pub const fn g(offset: u16, w: u8, h: u8, adv: u8, xo: i8, yo: i8) -> Glyph {
    Glyph { offset, w, h, adv, xo, yo }
}
pub struct Font {
    pub first: u8,
    pub bitmap: &'static [u8],
    pub glyphs: &'static [Glyph],
}
impl Font {
    fn glyph(&self, ch: u8) -> &Glyph {
        &self.glyphs[(ch - self.first) as usize]
    }
}

// sin/cos in integer maths: newlib's sinf/cosf cost ~40 us each here (~800 calls a frame).
// The angle becomes a 32-bit phase (one turn = 2^32), folded to a quadrant, then a Q30 Taylor
// polynomial up to x^11 / x^12; error ~1e-7, on par with float itself.
fn sin_phase(ph: u32) -> f32 {
    let x = ((ph & 0x3FFF_FFFF) as i64 * 1686629713) >> 30; // pi/2 in Q30
    let x2 = (x * x) >> 30;
    let mut t: i64;
    if ph & 0x4000_0000 != 0 {
        // 2nd/4th quadrant: cos of the angle within it
        t = 2;
        for c in [-296, 26631, -1491308, 44739243, -536870912, 1 << 30] {
            t = c + ((x2 * t) >> 30);
        }
    } else {
        t = -27;
        for c in [2959, -213040, 8947849, -178956971, 1 << 30] {
            t = c + ((x2 * t) >> 30);
        }
        t = (x * t) >> 30;
    }
    (if ph & 0x8000_0000 != 0 { -t } else { t }) as f32 * (1.0 / (1u32 << 30) as f32)
}
// rad * 2^24 is exact in float; the turn conversion (x 256 / 2pi, in Q26) stays in integers.
fn rad_phase(rad: f32) -> u32 {
    ((roundf(rad * 16777216.0) as i64 * 2734261102) >> 26) as u32
}
pub fn fsin(rad: f32) -> f32 {
    sin_phase(rad_phase(rad))
}
pub fn fcos(rad: f32) -> f32 {
    sin_phase(rad_phase(rad).wrapping_add(0x4000_0000))
}

fn fx(v: f32) -> i32 {
    roundf(v * 65536.0) as i32
}
// Coverage 0..32 from signed distance to an edge (negative = inside).
fn cover(d: f32) -> i32 {
    if d <= -0.5 {
        32
    } else if d >= 0.5 {
        0
    } else {
        ((0.5 - d) * 32.0) as i32
    }
}
fn cover_fx(d: i32) -> i32 {
    if d <= -32768 {
        32
    } else if d >= 32768 {
        0
    } else {
        (32768 - d) >> 11
    }
}
/// k = 0 -> a, 32 -> b (native RGB565).
fn mix(a: u16, b: u16, k: i32) -> u16 {
    let (a, b) = (a as i32, b as i32);
    let r = ((a >> 11) * (32 - k) + (b >> 11) * k) >> 5;
    let g = (((a >> 5) & 63) * (32 - k) + ((b >> 5) & 63) * k) >> 5;
    let bl = ((a & 31) * (32 - k) + (b & 31) * k) >> 5;
    (r << 11 | g << 5 | bl) as u16
}
fn isqrt(mut v: u32) -> u32 {
    // integer sqrt, far cheaper than soft-float sqrtf
    let mut r = 0u32;
    let mut b = 1u32 << 30;
    while b != 0 {
        if v >= r + b {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    r
}
// Narrows columns t in [lo, hi] to those where d + step * t <= lim (16.16 distances along a row).
fn below(d: i32, step: i32, lim: i32, lo: &mut i32, hi: &mut i32) {
    let a = lim - d;
    if step > 0 {
        *hi = (*hi).min(if a >= 0 { a / step } else { -((-a + step - 1) / step) }); // floor(a / step)
    } else if step < 0 {
        *lo = (*lo).max(if a <= 0 { (-a - step - 1) / -step } else { -(a / -step) }); // ceil(a / step)
    } else if a < 0 {
        *hi = *lo - 1;
    }
}

pub const W: usize = 240;
pub const STRIP: usize = 120;
/// Rows per half strip: one SPI DMA transfer is at most 32736 bytes, so each half goes on its own.
pub const HALF: usize = STRIP / 2;

/// Draws into one strip held as two halves of HALF rows each (separate DMA buffers on the board).
pub struct Smooth<'a> {
    halves: [&'a mut [u16]; 2],
    /// First screen row held in the strip.
    pub y0: i32,
    clip_in: i32, // optional circular clip, squared radii in doubled pixel units
    clip_out: i32,
    clip_r: f32,
}

impl<'a> Smooth<'a> {
    /// top and bottom hold W * HALF pixels each.
    pub fn new(top: &'a mut [u16], bottom: &'a mut [u16]) -> Self {
        assert!(top.len() == W * HALF && bottom.len() == W * HALF);
        Smooth { halves: [top, bottom], y0: 0, clip_in: 0, clip_out: 0, clip_r: 0.0 }
    }

    /// Starts the strip at screen row y0: the scene then draws only that strip's rows.
    pub fn strip(&mut self, y0: i32) {
        self.y0 = y0;
        self.clip(0.0);
    }

    /// Clip later shapes to a circle of radius r around the screen centre; r = 0 turns it off.
    pub fn clip(&mut self, r: f32) {
        self.clip_r = r;
        self.clip_in = if r != 0.0 { ((2.0 * r - 1.0) * (2.0 * r - 1.0)) as i32 } else { 0 };
        self.clip_out = if r != 0.0 { ((2.0 * r + 1.0) * (2.0 * r + 1.0)) as i32 } else { 0 };
    }

    /// c is in buffer (panel) byte order.
    fn plot(&mut self, x: i32, y: i32, mut k: i32, c: u16) {
        if k <= 0 || x < 0 || x >= W as i32 || y < self.y0 || y >= self.y0 + STRIP as i32 {
            return;
        }
        if self.clip_out != 0 {
            // int test: a float compare here is a soft-float call on every pixel
            let (cx, cy) = (2 * x + 1 - 240, 2 * y + 1 - 240);
            let d = cx * cx + cy * cy;
            if d >= self.clip_out {
                return;
            }
            if d > self.clip_in {
                k = k * cover((d as f32 / 4.0 - self.clip_r * self.clip_r) / (2.0 * self.clip_r)) >> 5;
            }
        }
        let p = &mut self.row(y)[x as usize];
        *p = if k >= 32 { c } else { mix(p.swap_bytes(), c.swap_bytes(), k).swap_bytes() };
    }

    fn skip(&self, top: f32, bottom: f32) -> bool {
        bottom < self.y0 as f32 || top >= (self.y0 + STRIP as i32) as f32
    }

    // Narrows a row's columns [a, b] to the screen and the clip circle, so loops skip pixels plot() would drop.
    fn row_span(&self, y: i32, a: &mut i32, b: &mut i32) {
        *a = (*a).max(0);
        *b = (*b).min(W as i32 - 1);
        if self.clip_out == 0 {
            return;
        }
        let yy = 2 * y + 1 - 240;
        let r2 = self.clip_out - 1 - yy * yy;
        if r2 < 0 {
            *b = *a - 1;
            return;
        }
        let m = isqrt(r2 as u32) as i32; // |2x + 1 - 240| <= m keeps D < clipOut
        *a = (*a).max((240 - m) >> 1);
        *b = (*b).min((239 + m) >> 1);
    }

    // Rows of the current strip that overlap [top, bottom).
    fn row_lo(&self, top: f32) -> i32 {
        self.y0.max(floorf(top) as i32)
    }
    fn row_hi(&self, bottom: f32) -> i32 {
        (self.y0 + STRIP as i32).min(ceilf(bottom) as i32)
    }
    fn row(&mut self, y: i32) -> &mut [u16] {
        let r = (y - self.y0) as usize;
        let i = r % HALF * W;
        &mut self.halves[r / HALF][i..i + W]
    }

    pub fn fill(&mut self, c: u16) {
        for h in &mut self.halves {
            h.fill(c.swap_bytes());
        }
    }

    /// Filled circle; with c1 != c0 it is shaded diagonally from c0 (top-left) to c1 (bottom-right).
    /// hole > 0 skips the middle (radius hole - 1) when a later shape will cover it anyway.
    pub fn circle_grad(&mut self, cx: f32, cy: f32, r: f32, c0: u16, c1: u16, hole: f32) {
        if self.skip(cy - r - 1.0, cy + r + 1.0) {
            return;
        }
        // Positions in 24.8 fixed point; their squares carry 16 fraction bits, so sqrt gives 8 again.
        let cx8 = roundf(cx * 256.0) as i32;
        let cy8 = roundf(cy * 256.0) as i32;
        let out2 = roundf((r + 0.5) * (r + 0.5) * 65536.0) as i32;
        let in2 = if r > 0.5 { roundf((r - 0.5) * (r - 0.5) * 65536.0) as i32 } else { -1 };
        let hole2 = if hole > 1.0 { roundf((hole - 1.0) * (hole - 1.0) * 65536.0) as i32 } else { -1 };
        let r2 = roundf(r * r * 65536.0) as i32;
        let inv2r = roundf(65536.0 / (2.0 * r)) as i64; // edge distance (|p|^2 - r^2) / 2r, in 16.16
        let g = roundf(32.0 / (2.8 * r) * 65536.0) as i64; // gradient step per pixel, 16.16
        let mut shade = [0u16; 33]; // the gradient's 33 levels, mixed once
        if c0 != c1 {
            for (k, s) in shade.iter_mut().enumerate() {
                *s = mix(c0, c1, k as i32).swap_bytes();
            }
        }
        let flat = c0.swap_bytes();
        for y in self.row_lo(cy - r - 1.0)..self.row_hi(cy + r + 1.0) {
            let dy8 = y * 256 + 128 - cy8;
            let dy2 = dy8 * dy8;
            if dy2 >= out2 {
                continue;
            }
            let out = isqrt((out2 - dy2) as u32) as i32;
            let inn = if dy2 < in2 { isqrt((in2 - dy2) as u32) as i32 } else { -1 };
            let hs = if dy2 < hole2 { isqrt((hole2 - dy2) as u32) as i32 } else { -1 };
            // Pixel x is inside a half-span h when |x*256 + 128 - cx8| <= h.
            let xa = (cx8 - out - 128 + 255) >> 8;
            let xb = (cx8 + out - 128) >> 8;
            let (sa, sb) = if inn < 0 { (1, 0) } else { ((cx8 - inn - 128 + 255) >> 8, (cx8 + inn - 128) >> 8) };
            let (ha, hb) = if hs < 0 { (-1, -2) } else { ((cx8 - hs - 128 + 255) >> 8, (cx8 + hs - 128) >> 8) };
            let (mut lo, mut hi) = (xa, xb);
            self.row_span(y, &mut lo, &mut hi);
            let mut kf = ((((lo * 256 + 128 - cx8 + dy8) as i64 * g) >> 8) + (16 << 16)) as i32;
            let g = g as i32;
            let mut x = lo;
            while x <= hi {
                if x >= ha && x <= hb {
                    // jump over the hole
                    kf += g * (hb - x);
                    x = hb;
                } else {
                    let c = if c0 == c1 { flat } else { shade[(kf >> 16).clamp(0, 32) as usize] };
                    if x < sa || x > sb {
                        // edge pixel: |p| - r ~= (|p|^2 - r^2) / 2r, no sqrt needed
                        let dx8 = x * 256 + 128 - cx8;
                        self.plot(x, y, cover_fx((((dx8 * dx8 + dy2 - r2) as i64 * inv2r) >> 16) as i32), c);
                    } else if self.clip_out != 0 {
                        self.plot(x, y, 32, c);
                    } else {
                        self.row(y)[x as usize] = c; // fully covered, on screen and unclipped
                    }
                }
                x += 1;
                kf += g;
            }
        }
    }
    pub fn circle(&mut self, cx: f32, cy: f32, r: f32, c: u16) {
        self.circle_grad(cx, cy, r, c, c, 0.0);
    }

    /// Convex polygon from n (x, y) pairs (n <= 8), either winding.
    pub fn poly(&mut self, p: &[f32], c: u16) {
        let n = p.len() / 2;
        let c = c.swap_bytes();
        let (mut area, mut lo, mut hi, mut l, mut rr) = (0.0f32, 1e9f32, -1e9f32, 1e9f32, -1e9f32);
        for i in 0..n {
            let j = (i + 1) % n;
            area += p[2 * i] * p[2 * j + 1] - p[2 * j] * p[2 * i + 1];
            lo = lo.min(p[2 * i + 1]);
            hi = hi.max(p[2 * i + 1]);
            l = l.min(p[2 * i]);
            rr = rr.max(p[2 * i]);
        }
        if self.skip(lo - 1.0, hi + 1.0) {
            return;
        }
        let s = if area > 0.0 { 1.0 } else { -1.0 };
        let (mut nx, mut ny, mut nc) = ([0f32; 8], [0f32; 8], [0f32; 8]);
        for i in 0..n {
            // outward unit normals: distance d = nx*x + ny*y + nc
            let j = (i + 1) % n;
            let (ex, ey) = (p[2 * j] - p[2 * i], p[2 * j + 1] - p[2 * i + 1]);
            let len = hypotf(ex, ey);
            nx[i] = s * ey / len;
            ny[i] = -s * ex / len;
            nc[i] = -(nx[i] * p[2 * i] + ny[i] * p[2 * i + 1]);
        }
        let xa = floorf(l - 1.0) as i32;
        let xb = ceilf(rr + 1.0) as i32;
        let (mut d, mut step, mut step_y) = ([0i32; 8], [0i32; 8], [0i32; 8]);
        let y_lo = self.row_lo(lo - 1.0);
        for i in 0..n {
            step[i] = fx(nx[i]);
            step_y[i] = fx(ny[i]);
            d[i] = fx(nx[i] * (xa as f32 + 0.5) + ny[i] * (y_lo as f32 + 0.5) + nc[i]);
        }
        for y in y_lo..self.row_hi(hi + 1.0) {
            // Distances are linear along the row (d + step * t, t = x - xa), so each edge bounds the
            // columns with any coverage (all d < half a pixel) and with full coverage (all d <= -half).
            let (mut t0, mut t1) = (0, xb - xa);
            let (mut f0, mut f1) = (0, t1);
            for i in 0..n {
                if y > y_lo {
                    d[i] += step_y[i];
                }
                below(d[i], step[i], 32767, &mut t0, &mut t1);
                below(d[i], step[i], -32768, &mut f0, &mut f1);
            }
            let (mut lo, mut hi) = (xa + t0, xa + t1);
            self.row_span(y, &mut lo, &mut hi);
            let mut x = lo;
            while x <= hi {
                let t = x - xa;
                if t >= f0 && t <= f1 && self.clip_out == 0 {
                    let e = (xa + f1).min(hi);
                    self.row(y)[x as usize..=e as usize].fill(c);
                    x = e + 1;
                    continue;
                }
                let mut m = d[0] + step[0] * t;
                for i in 1..n {
                    m = m.max(d[i] + step[i] * t);
                }
                self.plot(x, y, cover_fx(m), c);
                x += 1;
            }
        }
    }
    #[allow(clippy::too_many_arguments)]
    pub fn tri(&mut self, ax: f32, ay: f32, bx: f32, by: f32, cx: f32, cy: f32, c: u16) {
        self.poly(&[ax, ay, bx, by, cx, cy], c);
    }
    pub fn rect(&mut self, x: f32, y: f32, w: f32, h: f32, c: u16) {
        self.poly(&[x, y, x + w, y, x + w, y + h, x, y + h], c);
    }

    /// Ellipse rotated by deg (approximate distance, fine for small shapes like leaves).
    pub fn ellipse(&mut self, cx: f32, cy: f32, rx: f32, ry: f32, deg: f32, c: u16) {
        let big_r = rx.max(ry) + 1.0;
        if self.skip(cy - big_r, cy + big_r) {
            return;
        }
        let c = c.swap_bytes();
        let (s, co, m) = (fsin(rad(deg)), fcos(rad(deg)), rx.min(ry));
        let m_fx = fx(m) as i64;
        let qin = fx((1.0 - 0.5 / m) * (1.0 - 0.5 / m)) as i64;
        let qout = fx((1.0 + 0.5 / m) * (1.0 + 0.5 / m)) as i64;
        let (dux, dvx) = (fx(co / rx), fx(-s / ry));
        // Row spans: q = 1 is the quadratic form a dx^2 + 2b dx dy + c dy^2 = 1, so a level-S curve meets a row
        // at xc = cx - (b/a) dy, half width^2 = S/a - (ac - b^2)/a^2 dy^2. Only a sqrt per row, in ints.
        let (ix, iy) = (1.0 / (rx * rx), 1.0 / (ry * ry));
        let a = co * co * ix + s * s * iy;
        let bb = co * s * (ix - iy);
        let (cx16, cy16, kb) = (fx(cx), fx(cy), fx(-bb / a));
        let h_out = fx((qout as f32 / 65536.0 + 0.01) / a);
        let h_in = if qin > 1000 { fx((qin as f32 / 65536.0 - 0.01) / a) } else { -1 }; // padded inward
        let h1 = roundf(ix * iy / (a * a) * 4294967296.0) as i64;
        let xa = floorf(cx - big_r) as i32;
        let xb = ceilf(cx + big_r) as i32;
        let y_lo = self.row_lo(cy - big_r);
        let (dx0, dy0) = (xa as f32 + 0.5 - cx, y_lo as f32 + 0.5 - cy); // row-start (u, v) at the first row, then stepped
        let mut u0 = fx((dx0 * co + dy0 * s) / rx);
        let mut v0 = fx((-dx0 * s + dy0 * co) / ry);
        let (duy, dvy) = (fx(s / rx), fx(co / ry));
        for y in y_lo..self.row_hi(cy + big_r) {
            let dy16 = (y << 16) + 32768 - cy16;
            let xc = cx16 + ((kb as i64 * dy16 as i64) >> 16) as i32 - 32768;
            let sub = ((((h1 * dy16 as i64) >> 32) * dy16 as i64) >> 16) as i32;
            let (wo, wi) = (h_out - sub, h_in - sub);
            if wo >= 0 {
                let ho = (isqrt(wo as u32) << 8) as i32;
                let hi2 = if wi > 0 { (isqrt(wi as u32) << 8) as i32 } else { -1 }; // half widths, 16.16
                // Columns (pixel centre x + 0.5) inside each half width, a pixel of slack against rounding.
                let mut lo = xa.max(((xc - ho) >> 16) - 1);
                let mut hi = xb.min(((xc + ho + 65535) >> 16) + 1);
                let (ia, ib) = if hi2 < 0 || self.clip_out != 0 {
                    (1, 0)
                } else {
                    (((xc - hi2 + 65535) >> 16) + 1, ((xc + hi2) >> 16) - 1)
                };
                self.row_span(y, &mut lo, &mut hi);
                let mut u = u0 + dux * (lo - xa);
                let mut v = v0 + dvx * (lo - xa);
                let mut x = lo;
                while x <= hi {
                    if x >= ia && x <= ib {
                        // interior: fill straight through
                        let e = ib.min(hi);
                        self.row(y)[x as usize..=e as usize].fill(c);
                        u += dux * (e - x);
                        v += dvx * (e - x);
                        x = e;
                    } else {
                        let q = (u as i64 * u as i64 + v as i64 * v as i64) >> 16;
                        if q < qout {
                            if q <= qin {
                                self.plot(x, y, 32, c);
                            } else {
                                let e = q - 65536;
                                let root = (e >> 1) - ((e * e) >> 19); // sqrt(1 + e) - 1 ~= e/2 - e^2/8, 16.16
                                self.plot(x, y, cover_fx(((root * m_fx) >> 16) as i32), c);
                            }
                        }
                    }
                    x += 1;
                    u += dux;
                    v += dvx;
                }
            }
            u0 += duy;
            v0 += dvy;
        }
    }

    /// Rounded rectangle w x h with corner radius rad, centred on (cx, cy), rotated by deg.
    #[allow(clippy::too_many_arguments)]
    pub fn rrect(&mut self, cx: f32, cy: f32, w: f32, h: f32, radius: f32, deg: f32, c: u16) {
        if self.skip(cy - (w + h) / 2.0 - 1.0, cy + (w + h) / 2.0 + 1.0) {
            return; // loose bound before any trig
        }
        let c = c.swap_bytes();
        let (s, co) = (fsin(rad(deg)), fcos(rad(deg)));
        let rx = fabsf(w / 2.0 * co) + fabsf(h / 2.0 * s) + 1.0;
        let ry = fabsf(w / 2.0 * s) + fabsf(h / 2.0 * co) + 1.0;
        let (ex, ey, fr) = (fx(w / 2.0 - radius), fx(h / 2.0 - radius), fx(radius)); // straight-edge half extents
        let (dux, dvx) = (fx(co), fx(-s));
        let xa = floorf(cx - rx) as i32;
        let xb = ceilf(cx + rx) as i32;
        let y_lo = self.row_lo(cy - ry);
        let (dx, dy) = (xa as f32 + 0.5 - cx, y_lo as f32 + 0.5 - cy);
        let mut u0 = fx(dx * co + dy * s); // rectangle's own axes
        let mut v0 = fx(-dx * s + dy * co);
        let (duy, dvy) = (fx(s), fx(co));
        for y in y_lo..self.row_hi(cy + ry) {
            let (mut u, mut v) = (u0, v0);
            // u and v are linear along the row: bound the columns that can be covered (|u|, |v| within the
            // corner reach) and those fully inside (straight-edge zone at least half a pixel in).
            let (mut t0, mut t1) = (0, xb - xa);
            let (mut f0, mut f1) = (0, t1);
            let (ox, oy, inn) = (ex + fr + 65536, ey + fr + 65536, 0.min(fr - 32768));
            below(u, dux, ox, &mut t0, &mut t1);
            below(-u, -dux, ox, &mut t0, &mut t1);
            below(v, dvx, oy, &mut t0, &mut t1);
            below(-v, -dvx, oy, &mut t0, &mut t1);
            below(u, dux, ex + inn, &mut f0, &mut f1);
            below(-u, -dux, ex + inn, &mut f0, &mut f1);
            below(v, dvx, ey + inn, &mut f0, &mut f1);
            below(-v, -dvx, ey + inn, &mut f0, &mut f1);
            let (mut lo, mut hi) = (xa + t0, xa + t1);
            self.row_span(y, &mut lo, &mut hi);
            if self.clip_out != 0 {
                f1 = f0 - 1;
            }
            u += dux * (lo - xa);
            v += dvx * (lo - xa);
            let mut x = lo;
            while x <= hi {
                if x - xa >= f0 && x - xa <= f1 {
                    // interior: fill straight through
                    let e = (xa + f1).min(hi);
                    self.row(y)[x as usize..=e as usize].fill(c);
                    u += dux * (e - x);
                    v += dvx * (e - x);
                    x = e;
                } else {
                    let (qx, qy) = (u.abs() - ex, v.abs() - ey);
                    if qx <= fr + 65536 && qy <= fr + 65536 {
                        if qx > 0 && qy > 0 {
                            // corner: distance to the rounding circle, integer sqrt in 8.8
                            let (qx8, qy8) = (qx >> 8, qy >> 8);
                            self.plot(x, y, cover_fx(((isqrt((qx8 * qx8 + qy8 * qy8) as u32) << 8) as i32) - fr), c);
                        } else {
                            self.plot(x, y, cover_fx(qx.max(qy) - fr), c);
                        }
                    }
                }
                x += 1;
                u += dux;
                v += dvx;
            }
            u0 += duy;
            v0 += dvy;
        }
    }

    /// Ring stroke of the given width between angles a0 < a1, less than 180 degrees apart
    /// (degrees, 0 = right, 90 = down).
    #[allow(clippy::too_many_arguments)]
    pub fn arc(&mut self, cx: f32, cy: f32, r: f32, width: f32, a0: f32, a1: f32, c: u16) {
        let big_r = r + width / 2.0 + 1.0;
        let ri_box = (r - width / 2.0 - 1.0).max(0.0);
        if self.skip(cy - big_r, cy + big_r) {
            return;
        }
        let c = c.swap_bytes();
        let (s0, c0, s1, c1) = (fsin(rad(a0)), fcos(rad(a0)), fsin(rad(a1)), fcos(rad(a1)));
        // Bounding box: both ends at both radii, plus the outer extreme of any axis the arc crosses.
        let (rb, ro_) = (ri_box, big_r);
        let mut lo = cy + (rb * s0).min(ro_ * s0).min((rb * s1).min(ro_ * s1));
        let mut hi = cy + (rb * s0).max(ro_ * s0).max((rb * s1).max(ro_ * s1));
        let mut l = cx + (rb * c0).min(ro_ * c0).min((rb * c1).min(ro_ * c1));
        let mut rr = cx + (rb * c0).max(ro_ * c0).max((rb * c1).max(ro_ * c1));
        let mut q = -180;
        while q <= 180 {
            if (q as f32) > a0 && (q as f32) < a1 {
                match q {
                    0 => rr = cx + big_r,
                    90 => hi = cy + big_r,
                    -180 | 180 => l = cx - big_r,
                    -90 => lo = cy - big_r,
                    _ => {}
                }
            }
            q += 90;
        }
        if self.skip(lo, hi) {
            return;
        }
        // Squared-distance limits of the band in doubled pixel units, so most of the box is rejected with ints.
        let mut inn = (2.0 * (r - width / 2.0) - 1.0).max(0.0) as i32;
        let mut out = (2.0 * (r + width / 2.0) + 1.0) as i32;
        let (cx2, cy2) = (roundf(2.0 * cx) as i32, roundf(2.0 * cy) as i32);
        let mut fin = (2.0 * (r - width / 2.0) + 1.0) as i32; // fully inside between these
        let mut fout = (2.0 * (r + width / 2.0) - 1.0).max(0.0) as i32;
        inn *= inn;
        out *= out;
        fin *= fin;
        fout *= fout;
        // Angle limits as 12-bit fixed-point directions, so the side test is integer too.
        let (sa0, ca0, sa1, ca1) = ((s0 * 4096.0) as i32, (c0 * 4096.0) as i32, (s1 * 4096.0) as i32, (c1 * 4096.0) as i32);
        // Edge distance (|p|^2 - e^2) / 2e for each edge radius e, kept in ints: |p|^2 = D / 4.
        let (ri, ro) = (r - width / 2.0, r + width / 2.0);
        let (ro4, ri4) = (roundf(4.0 * ro * ro) as i32, roundf(4.0 * ri * ri) as i32);
        let k_out = roundf(65536.0 * 65536.0 / (8.0 * ro)) as i64;
        let k_in = if ri > 0.0 { roundf(65536.0 * 65536.0 / (8.0 * ri)) as i64 } else { 0 };
        for y in self.row_lo(lo)..self.row_hi(hi) {
            let yy = 2 * y + 1 - cy2;
            let rest = out - 1 - yy * yy;
            if rest < 0 {
                continue;
            }
            // Only columns between the outer and inner circle: |X| <= m keeps D < out, |X| <= h keeps D <= in.
            let m = isqrt(rest as u32) as i32;
            let h = if inn - yy * yy >= 0 { isqrt((inn - yy * yy) as u32) as i32 } else { -1 };
            let mut xa = (floorf(l) as i32).max((cx2 - m) >> 1);
            let mut xb = (ceilf(rr) as i32).min((cx2 - 1 + m) >> 1);
            let (ha, hb) = ((cx2 - h) >> 1, (cx2 - 1 + h) >> 1); // hole columns; ha > hb when there is none
            self.row_span(y, &mut xa, &mut xb);
            let mut x = xa;
            while x <= xb {
                if x >= ha && x <= hb {
                    x = hb + 1;
                    continue;
                }
                let xx = 2 * x + 1 - cx2;
                let d = xx * xx + yy * yy;
                x += 1;
                if d <= inn || d >= out {
                    continue;
                }
                // Distance inside each end ray, in 1/8192 px (doubled coords x 4096); feather the ends over a pixel.
                let (e0, e1) = (ca0 * yy - sa0 * xx, xx * sa1 - yy * ca1);
                if e0 <= -4096 || e1 <= -4096 {
                    continue; // over half a pixel outside a0..a1
                }
                if d > fin && d < fout && e0 >= 4096 && e1 >= 4096 {
                    self.plot(x - 1, y, 32, c);
                    continue;
                }
                // Edge pixels only: distance to each edge radius e is ~= (|p|^2 - e^2) / 2e near that edge.
                let d_out = (((d - ro4) as i64 * k_out) >> 16) as i32;
                let d_in = if ri > 0.0 { (((ri4 - d) as i64 * k_in) >> 16) as i32 } else { i32::MIN };
                let d_end = -8 * e0.min(e1); // to 16.16 px, positive outside
                self.plot(x - 1, y, cover_fx(d_out.max(d_in).max(d_end)), c);
            }
        }
    }

    /// One glyph drawn at `scale`, its centre at (cx, cy), rotated by deg.
    /// Fonts are stored large (4x) and supersampled 4x4 here, which gives smooth edges.
    #[allow(clippy::too_many_arguments)]
    pub fn glyph(&mut self, f: &Font, scale: f32, ch: u8, cx: f32, cy: f32, deg: f32, c: u16) {
        let g = f.glyph(ch);
        if g.w == 0 {
            return;
        }
        let c = c.swap_bytes();
        let top = f.glyphs.iter().map(|g| g.yo as i32).fold(0, i32::min); // cap height for a capitals-only font
        let (gw, gh, gxo, gyo) = (g.w as i32, g.h as i32, g.xo as f32, g.yo as f32);
        let (ax, ay) = (g.adv as f32 / 2.0, top as f32 / 2.0); // anchor: advance centre, cap-height middle
        let rb = (fabsf(gxo - ax) + gw as f32 + fabsf(gyo - ay) + gh as f32) * scale; // loose bound
        if self.skip(cy - rb, cy + rb) {
            return;
        }
        let (s, co) = (fsin(rad(deg)), fcos(rad(deg)));

        let (mut lo, mut hi, mut l, mut rr) = (1e9f32, -1e9f32, 1e9f32, -1e9f32); // screen box of the rotated glyph box
        for i in 0..4 {
            let u = (gxo + (i & 1) as f32 * gw as f32 - ax) * scale;
            let v = (gyo + (i >> 1) as f32 * gh as f32 - ay) * scale;
            let (x, y) = (cx + u * co - v * s, cy + u * s + v * co);
            lo = lo.min(y);
            hi = hi.max(y);
            l = l.min(x);
            rr = rr.max(x);
        }

        // Font-space position (u, v) of a screen point is linear in x and y; step it in fixed point.
        let (usx, vsx) = (fx(co / scale / 4.0), fx(-s / scale / 4.0));
        let (usy, vsy) = (fx(s / scale / 4.0), fx(co / scale / 4.0));
        let bits = &f.bitmap[g.offset as usize..];
        let xa = floorf(l) as i32;
        let xb = ceilf(rr) as i32;
        let y_lo = self.row_lo(lo);
        let (dx, dy) = (xa as f32 + 0.125 - cx, y_lo as f32 + 0.125 - cy);
        let mut u0 = fx((dx * co + dy * s) / scale + ax - gxo);
        let mut v0 = fx((-dx * s + dy * co) / scale + ay - gyo);
        let (duy, dvy) = (fx(s / scale), fx(co / scale)); // row-start step
        let (u_min, u_max) = (3 * (usx.min(0) + usy.min(0)), 3 * (usx.max(0) + usy.max(0)));
        let (v_min, v_max) = (3 * (vsx.min(0) + vsy.min(0)), 3 * (vsx.max(0) + vsy.max(0)));
        for y in y_lo..self.row_hi(hi) {
            let (mut u, mut v) = (u0, v0);
            // Skip columns whose 4x4 samples all miss the glyph box (rotated glyphs leave empty box corners).
            let (mut t0, mut t1) = (0, xb - xa);
            below(u + u_min, 4 * usx, (gw << 16) - 1, &mut t0, &mut t1);
            below(-(u + u_max), -4 * usx, 0, &mut t0, &mut t1);
            below(v + v_min, 4 * vsx, (gh << 16) - 1, &mut t0, &mut t1);
            below(-(v + v_max), -4 * vsx, 0, &mut t0, &mut t1);
            let (mut lo, mut hi) = (xa + t0, xa + t1);
            self.row_span(y, &mut lo, &mut hi);
            u += 4 * usx * (lo - xa);
            v += 4 * vsx * (lo - xa);
            for x in lo..=hi {
                let mut hits = 0;
                let (mut uu, mut vv) = (u, v);
                for _ in 0..4 {
                    for _ in 0..4 {
                        let (iu, iv) = ((uu >> 16) as u32, (vv >> 16) as u32); // negative wraps to huge, failing the bounds test
                        if iu < gw as u32 && iv < gh as u32 {
                            let bit = (iv * gw as u32 + iu) as usize;
                            hits += (bits[bit >> 3] >> (7 - (bit & 7))) as i32 & 1;
                        }
                        uu += usx;
                        vv += vsx;
                    }
                    uu += usy - 4 * usx;
                    vv += vsy - 4 * vsx;
                }
                self.plot(x, y, hits * 2, c);
                u += 4 * usx;
                v += 4 * vsx;
            }
            u0 += duy;
            v0 += dvy;
        }
    }

    pub fn advance(f: &Font, scale: f32, ch: u8) -> f32 {
        f.glyph(ch).adv as f32 * scale
    }

    /// Upright text centred on (cx, cy).
    pub fn text(&mut self, f: &Font, scale: f32, s: &str, cx: f32, cy: f32, c: u16) {
        let w: f32 = s.bytes().map(|ch| Self::advance(f, scale, ch)).sum();
        let mut x = cx - w / 2.0;
        for ch in s.bytes() {
            let a = Self::advance(f, scale, ch);
            self.glyph(f, scale, ch, x + a / 2.0, cy, 0.0, c);
            x += a;
        }
    }

    /// Text along a circle of radius r around the screen centre, centred on angle deg.
    /// Top text reads clockwise; bottom = true reads counter-clockwise so it stays upright.
    #[allow(clippy::too_many_arguments)]
    pub fn arc_text(&mut self, f: &Font, scale: f32, s: &str, r: f32, deg: f32, spacing: f32, c: u16, bottom: bool) {
        let dir = if bottom { -1.0f32 } else { 1.0 };
        let mut len = 0.0f32;
        for ch in s.bytes() {
            len += Self::advance(f, scale, ch) + spacing;
        }
        let mut a = (deg as f64 - (dir * (len - spacing) / 2.0 / r) as f64 * RAD_TO_DEG) as f32;
        for ch in s.bytes() {
            let half = ((dir * Self::advance(f, scale, ch) / 2.0 / r) as f64 * RAD_TO_DEG) as f32;
            a += half;
            self.glyph(f, scale, ch, 120.0 + r * fcos(rad(a)), 120.0 + r * fsin(rad(a)), a + 90.0 * dir, c);
            a = (a as f64 + (half as f64 + (dir * spacing / r) as f64 * RAD_TO_DEG)) as f32;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn integer_sin_cos_stay_float_accurate() {
        let mut a = -20.0f32;
        while a < 20.0 {
            assert!((fsin(a) as f64 - (a as f64).sin()).abs() < 1e-6, "sin {a}");
            assert!((fcos(a) as f64 - (a as f64).cos()).abs() < 1e-6, "cos {a}");
            a += 0.0007;
        }
    }
}
