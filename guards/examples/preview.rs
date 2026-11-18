//! Renders every guard to preview/guard_N.ppm on the PC with the same code the board runs.
//! Usage: tools/preview/run.sh   -> writes preview/guard_N.png
use guards::smooth::{HALF, STRIP, Smooth, W};
use std::io::Write;

fn save(scene: &dyn Fn(&mut Smooth), name: &str) {
    let mut ppm = format!("P6 {W} {W} 255\n").into_bytes();
    let (mut top, mut bottom) = (vec![0u16; W * HALF], vec![0u16; W * HALF]);
    for y0 in (0..W).step_by(STRIP) {
        let mut s = Smooth::new(&mut top, &mut bottom);
        s.strip(y0 as i32);
        scene(&mut s);
        for p in top.iter().chain(&bottom).map(|p| p.swap_bytes()) {
            // the buffer is in panel byte order
            ppm.extend([((p >> 11) << 3) as u8, (((p >> 5) & 63) << 2) as u8, ((p & 31) << 3) as u8]);
        }
    }
    std::fs::File::create(name).and_then(|mut f| f.write_all(&ppm)).expect(name);
}

fn main() {
    for (i, scene) in guards::GUARDS.iter().enumerate() {
        save(scene, &format!("preview/guard_{i}.ppm"));
    }
    save(&|s| guards::no_guard(s, 5), "preview/no_guard.ppm");
}
