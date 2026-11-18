//! GC9A01 240x240 round panel over SPI with DMA. RST is not wired, so there is no hardware reset.
use embassy_time::Timer;
use esp_hal::{
    Async,
    dma::DmaTxBuf,
    dma_tx_buffer,
    gpio::Output,
    spi::master::SpiDma,
};
use guards::smooth::{HALF, STRIP, Smooth, W};

// Init sequence from Arduino_GFX's Arduino_GC9A01 (what the C++ firmware ran on this panel):
// (command, data). 0x11 (sleep out) and 0x29 (display on) follow with their delays.
const INIT: &[(u8, &[u8])] = &[
    (0xEF, &[]),
    (0xEB, &[0x14]),
    (0xFE, &[]),
    (0xEF, &[]),
    (0xEB, &[0x14]),
    (0x84, &[0x40]),
    (0x85, &[0xFF]),
    (0x86, &[0xFF]),
    (0x87, &[0xFF]),
    (0x88, &[0x0A]),
    (0x89, &[0x21]),
    (0x8A, &[0x00]),
    (0x8B, &[0x80]),
    (0x8C, &[0x01]),
    (0x8D, &[0x01]),
    (0x8E, &[0xFF]),
    (0x8F, &[0xFF]),
    (0xB6, &[0x00, 0x20]),
    (0x3A, &[0x05]), // 16-bit RGB565
    (0x90, &[0x08, 0x08, 0x08, 0x08]),
    (0xBD, &[0x06]),
    (0xBC, &[0x00]),
    (0xFF, &[0x60, 0x01, 0x04]),
    (0xC3, &[0x13]),
    (0xC4, &[0x13]),
    (0xC9, &[0x22]),
    (0xBE, &[0x11]),
    (0xE1, &[0x10, 0x0E]),
    (0xDF, &[0x21, 0x0C, 0x02]),
    (0xF0, &[0x45, 0x09, 0x08, 0x08, 0x26, 0x2A]),
    (0xF1, &[0x43, 0x70, 0x72, 0x36, 0x37, 0x6F]),
    (0xF2, &[0x45, 0x09, 0x08, 0x08, 0x26, 0x2A]),
    (0xF3, &[0x43, 0x70, 0x72, 0x36, 0x37, 0x6F]),
    (0xED, &[0x1B, 0x0B]),
    (0xAE, &[0x77]),
    (0xCD, &[0x63]),
    (0x70, &[0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03]),
    (0xE8, &[0x34]),
    (0x62, &[0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70]),
    (0x63, &[0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70]),
    (0x64, &[0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07]),
    (0x66, &[0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00]),
    (0x67, &[0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98]),
    (0x74, &[0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00]),
    (0x98, &[0x3E, 0x07]),
    (0x35, &[]),
    (0x36, &[0x08]), // MADCTL: rotation 0, BGR panel
    (0x21, &[]),     // inversion on: the panel is IPS
];
const SLPOUT: u8 = 0x11;
const SLPIN: u8 = 0x10;
const DISPON: u8 = 0x29;
const CASET: u8 = 0x2A;
const RASET: u8 = 0x2B;
const RAMWR: u8 = 0x2C;

pub struct Display {
    // Options only while a DMA transfer owns them; always Some between calls.
    spi: Option<SpiDma<'static, Async>>,
    dc: Output<'static>,
    cmd: Option<DmaTxBuf>,
    halves: [Option<DmaTxBuf>; 2], // the strip the renderer draws into, one DMA transfer per half
}

impl Display {
    pub async fn new(spi: SpiDma<'static, Async>, dc: Output<'static>) -> Self {
        let mut d = Display {
            spi: Some(spi),
            dc,
            cmd: Some(dma_tx_buffer!(16).unwrap()),
            halves: [Some(dma_tx_buffer!(W * HALF * 2).unwrap()), Some(dma_tx_buffer!(W * HALF * 2).unwrap())],
        };
        for &(c, data) in INIT {
            d.command(c, data).await;
        }
        d.wake().await;
        d
    }

    /// Panel sleep: keeps its image, so no redraw on wake.
    pub async fn sleep(&mut self) {
        self.command(SLPIN, &[]).await;
        Timer::after_millis(120).await;
    }
    pub async fn wake(&mut self) {
        self.command(SLPOUT, &[]).await;
        Timer::after_millis(120).await;
        self.command(DISPON, &[]).await;
    }

    /// Draws a scene: renders each 120-row strip, then pushes it while the CPU idles.
    pub async fn draw(&mut self, scene: &dyn Fn(&mut Smooth)) {
        for y0 in (0..W).step_by(STRIP) {
            {
                let [top, bottom] = &mut self.halves;
                let top = bytemuck::cast_slice_mut(top.as_mut().unwrap().as_mut_slice());
                let bottom = bytemuck::cast_slice_mut(bottom.as_mut().unwrap().as_mut_slice());
                let mut s = Smooth::new(top, bottom);
                s.strip(y0 as i32);
                scene(&mut s);
            }
            for h in 0..2 {
                let y = (y0 + h * HALF) as u16;
                self.command(CASET, &[0, 0, 0, W as u8 - 1]).await;
                self.command(RASET, &[(y >> 8) as u8, y as u8, ((y + HALF as u16 - 1) >> 8) as u8, (y + HALF as u16 - 1) as u8])
                    .await;
                self.command(RAMWR, &[]).await;
                Self::send(&mut self.spi, &mut self.halves[h], None).await;
            }
        }
    }

    async fn command(&mut self, c: u8, data: &[u8]) {
        self.dc.set_low();
        Self::send(&mut self.spi, &mut self.cmd, Some(&[c])).await;
        if !data.is_empty() {
            self.dc.set_high();
            Self::send(&mut self.spi, &mut self.cmd, Some(data)).await;
        }
        self.dc.set_high();
    }

    /// One DMA transfer of `data` (copied into buf), or of buf's whole contents when None.
    async fn send(spi: &mut Option<SpiDma<'static, Async>>, buf: &mut Option<DmaTxBuf>, data: Option<&[u8]>) {
        let mut b = buf.take().unwrap();
        if let Some(d) = data {
            b.fill(d);
        }
        let len = b.len();
        match spi.take().unwrap().write_buffer(len, b) {
            Ok(mut t) => {
                t.wait_for_done().await;
                let (s, b) = t.wait();
                *spi = Some(s);
                *buf = Some(b);
            }
            Err((e, s, b)) => {
                esp_println::println!("display: spi error {e:?}");
                *spi = Some(s);
                *buf = Some(b);
            }
        }
    }
}
