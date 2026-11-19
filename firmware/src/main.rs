//! Card guard firmware: draws a guard on the round screen, picks guards from the BOOT button, USB
//! serial or Bluetooth LE, and sleeps on a long press.
//!
//! Everything waits on events (button edge, serial byte, BLE write, timer) instead of polling, so the
//! CPU halts between them.
#![no_std]
#![no_main]
#![deny(clippy::mem_forget, reason = "mem::forget is generally not safe to do with esp_hal types")]

mod display;

use core::{cell::RefCell, fmt::Write as _};
use portable_atomic::{AtomicU8, AtomicU16, AtomicU32, Ordering};

use bt_hci::controller::ExternalController;
use embassy_executor::Spawner;
use esp_rtos::embassy::Executor;
use embassy_futures::select::{Either, Either3, select, select3};
use embassy_sync::{
    blocking_mutex::{Mutex, raw::CriticalSectionRawMutex as Cs},
    channel::Channel,
    signal::Signal,
};
use embassy_time::{Duration, Instant, Timer};
use embedded_io_async::Read as _;
use esp_backtrace as _;
use esp_hal::{
    Async,
    clock::CpuClock,
    gpio::{Event, Input, InputConfig, Level, Output, OutputConfig, Pull, WakeupConfig},
    rng::Rng,
    ledc::{
        LSGlobalClkSource, Ledc, LowSpeed,
        channel::{self, ChannelHW, ChannelIFace},
        timer::{self, TimerIFace},
    },
    rtc_cntl::sleep::{LowPower, RtcSleepConfig},
    spi::{
        Mode,
        master::{Config as SpiConfig, Spi},
    },
    time::Rate,
    timer::timg::TimerGroup,
    usb::usb_serial_jtag::{UsbSerialJtag, UsbSerialJtagRx},
};
use esp_radio::ble::controller::BleConnector;
use esp_storage::FlashStorage;
use guards::{GUARD_NAMES, GUARDS};
use heapless::{Deque, String, Vec};
use static_cell::StaticCell;
use trouble_host::prelude::*;

use display::Display;

extern crate alloc;
use alloc::boxed::Box;
use core::ffi::c_void;

// As esp_app_desc!() but with the git tag as the version (build.rs).
esp_bootloader_esp_idf::esp_app_desc!(
    env!("CARDGUARD_VERSION"),
    env!("CARGO_PKG_NAME"),
    esp_bootloader_esp_idf::BUILD_TIME,
    esp_bootloader_esp_idf::BUILD_DATE,
    esp_bootloader_esp_idf::ESP_IDF_COMPATIBLE_VERSION,
    esp_bootloader_esp_idf::MMU_PAGE_SIZE,
    0,
    u16::MAX,
    esp_bootloader_esp_idf::SECURE_VERSION
);

// Pins from the vendor's Factory_samples_without_touch sketch (ESP32-2424S012). RST is not wired.
// SCK 6, MOSI 7, DC 2, CS 10, backlight 3, BOOT button 9.

// ---- Log: USB serial, and BLE notifications to the web app's log panel ----

type Line = String<160>;
static LOG: Channel<Cs, Line, 8> = Channel::new();
/// The last lines, replayed to a client when it subscribes: boot lines (version, heap) come before any.
static BACKLOG: Mutex<Cs, RefCell<Deque<Line, 8>>> = Mutex::new(RefCell::new(Deque::new()));

/// Random per boot, so the web app can tell a restarted board's line numbers from the old ones.
static BOOT: AtomicU16 = AtomicU16::new(0);
static SEQ: AtomicU32 = AtomicU32::new(0);

/// Serial gets the plain line; BLE gets it as "<boot>.<n> line", so a client that sees the backlog again
/// on a reconnect can skip the lines it already showed.
pub fn log_line(args: core::fmt::Arguments) {
    BACKLOG.lock(|b| {
        let mut s = Line::new();
        let n = SEQ.fetch_add(1, Ordering::Relaxed) + 1;
        let _ = write!(s, "{:04x}.{n} ", BOOT.load(Ordering::Relaxed));
        let start = s.len();
        let _ = s.write_fmt(args);
        esp_println::println!("{}", &s[start..]);
        let mut b = b.borrow_mut();
        if b.is_full() {
            b.pop_front();
        }
        let _ = b.push_back(s.clone());
        let _ = LOG.try_send(s); // full: BLE is behind, the line still went to serial
    });
}
#[macro_export]
macro_rules! log {
    ($($t:tt)*) => { $crate::log_line(format_args!($($t)*)) };
}

// ---- Requests from the button, serial and BLE, handled one at a time by the app task ----

enum Cmd {
    Next,
    Pick(u32),
    Bright(u8),
    Sleep(u64), // seconds until a timer wake; 0 = only the button wakes
}
static CMD: Channel<Cs, Cmd, 4> = Channel::new();
/// Guard now on screen: the web app highlights it after a reconnect (read) and on any change (notify).
static SHOWN: Signal<Cs, u8> = Signal::new();

#[derive(Clone, Copy, PartialEq)]
enum Adv {
    Fast,
    Slow,
}
static ADV: Signal<Cs, Adv> = Signal::new();
static OPEN: AtomicU8 = AtomicU8::new(0); // open BLE connections

/// Guard on screen when the board went to sleep, tagged; survives the restart that wakes it.
#[esp_hal::ram(unstable(rtc_fast, persistent))]
static RESUME: AtomicU32 = AtomicU32::new(0);
const RESUME_TAG: u32 = 0xC6A2_D500;

// ---- Brightness, kept in the nvs partition (the C++ firmware's NVS data there is ignored) ----

const BRIGHT_AT: u32 = 0x9000;
const MAGIC: u8 = 0xCB;

// Each save appends a 4-byte record [tag, value, FF, FF] after the last one; the sector is erased only
// when no free slot is left, so ~1000 saves per erase. A torn or foreign record ends the log: the
// value before it is used and the next save erases.
fn brightness_log(flash: &mut FlashStorage) -> (Option<u8>, Option<u32>) {
    let mut sector = [0u8; FlashStorage::SECTOR_SIZE as usize];
    if flash.read(BRIGHT_AT, &mut sector).is_err() {
        return (None, None);
    }
    let mut last = None;
    for (i, r) in sector.as_chunks::<4>().0.iter().enumerate() {
        match *r {
            [MAGIC, b, 0xFF, 0xFF] if (10..=100).contains(&b) => last = Some(b),
            [0xFF, 0xFF, 0xFF, 0xFF] => return (last, Some(BRIGHT_AT + 4 * i as u32)),
            _ => break,
        }
    }
    (last, None)
}

fn load_brightness(flash: &mut FlashStorage) -> u8 {
    brightness_log(flash).0.unwrap_or(60)
}
fn save_brightness(flash: &mut FlashStorage, pct: u8) {
    let at = match brightness_log(flash).1 {
        Some(at) => Ok(at),
        None => flash.erase(BRIGHT_AT, BRIGHT_AT + FlashStorage::SECTOR_SIZE).map(|()| BRIGHT_AT),
    };
    if let Err(e) = at.and_then(|at| flash.write_nor(at, &[MAGIC, pct, 0xFF, 0xFF])) {
        log!("brightness: save failed {e:?}");
    }
}

#[esp_rtos::main]
async fn main(spawner: Spawner) -> ! {
    esp_println::logger::init_logger_from_env();
    // 80 MHz: lower power; BLE needs at least 80
    let p = esp_hal::init(esp_hal::Config::default().with_cpu_clock(CpuClock::_80MHz));
    esp_alloc::heap_allocator!(#[esp_hal::ram(reclaimed)] size: 66320); // all of the bootloader's leftover RAM
    // + the BLE thread's stack (12 KB, ~6 KB used at peak), from main RAM, which the main stack barely uses.
    esp_alloc::heap_allocator!(size: BLE_STACK);
    let timg0 = TimerGroup::new(p.TIMG0);
    esp_rtos::start(timg0.timer0, p.FROM_CPU_INTR0);

    let mut flash = FlashStorage::new(p.FLASH);
    let mut brightness = load_brightness(&mut flash);

    // Backlight: 20 kHz PWM (above hearing), 8-bit, squared so the slider feels even.
    let mut ledc = Ledc::new(p.LEDC);
    ledc.set_global_slow_clock(LSGlobalClkSource::APBClk);
    static BL_TIMER: StaticCell<timer::Timer<'static, LowSpeed>> = StaticCell::new();
    let bl_timer = BL_TIMER.init(ledc.timer::<LowSpeed>(timer::Number::Timer0));
    bl_timer
        .configure(timer::config::Config { duty: timer::config::Duty::Duty8Bit, clock_source: timer::LSClockSource::APBClk, frequency: Rate::from_khz(20) })
        .unwrap();
    let mut backlight = ledc.channel(channel::Number::Channel0, p.GPIO3);
    backlight.configure(channel::config::Config { timer: bl_timer, duty_pct: 0, drive_mode: esp_hal::gpio::DriveMode::PushPull }).unwrap();
    let set_backlight = |bl: &channel::Channel<'_, LowSpeed>, pct: u8| {
        let pct = pct as u32;
        bl.set_duty_hw(if pct == 0 { 0 } else { (pct * pct * 255 / 10000).max(1) });
    };
    set_backlight(&backlight, brightness);

    let mut btn = Input::new(p.GPIO9, InputConfig::default().with_pull(Pull::Up));

    // 80 MHz SPI (the library default was 40); DMA so the CPU idles during the push.
    let spi = Spi::new(p.SPI2, SpiConfig::default().with_frequency(Rate::from_mhz(80)).with_mode(Mode::_0))
        .unwrap()
        .with_sck(p.GPIO6)
        .with_mosi(p.GPIO7)
        .with_cs(p.GPIO10)
        .with_dma(p.DMA_CH0)
        .into_async();
    // Waking from sleep restarts the chip; RESUME carries the guard across (garbage at power-on fails the tag).
    let resume = RESUME.swap(0, Ordering::Relaxed);
    let woke = resume & !0xFF == RESUME_TAG && ((resume & 0xFF) as usize) < GUARDS.len();
    let mut current: u8 = if woke { resume as u8 } else { 0 };

    // Bluetooth LE first: a waking board advertises sooner (the panel still shows its image meanwhile).
    // Its tasks run in their own thread, one priority above this one, so BLE preempts a guard draw
    // (CPU-bound, up to ~80 ms per strip) instead of waiting for it. Not an interrupt executor: the
    // radio driver blocks on its own threads, which can't run under an interrupt.
    let connector = BleConnector::new(p.BT, Default::default()).unwrap();
    BOOT.store(Rng::new().random() as u16, Ordering::Relaxed); // radio on: the RNG has real entropy now; nothing logged before
    let args = Box::into_raw(Box::new((connector, current, brightness))).cast();
    // SAFETY: the thread takes ownership of args (Send) and never ends.
    unsafe { esp_radio_rtos_driver::task_create("ble", ble_thread, args, 1, None, BLE_STACK) };

    let (rx, tx) = UsbSerialJtag::new(p.USB_DEVICE).into_async().split();
    core::mem::drop(tx); // esp-println writes the serial output
    spawner.spawn(serial(rx).unwrap());

    let mut display = Display::new(spi, Output::new(p.GPIO2, Level::High, OutputConfig::default())).await;
    show(&mut display, current).await;

    log!("cardguard {}", env!("CARDGUARD_VERSION"));
    // After display + BLE are up, so it shows what designs and features actually leave free.
    log!("heap: {} KB free, {} KB total", esp_alloc::HEAP.free() / 1024, (esp_alloc::HEAP.free() + esp_alloc::HEAP.used()) / 1024);

    if woke {
        log!("power: woke");
        btn.wait_for_high().await; // the waking press shouldn't also switch designs
        Timer::after_millis(50).await;
    }

    let mut lp = LowPower::new(p.LPWR);
    let mut save_at: Option<Instant> = None; // brightness saved 2 s after the slider stops, not every drag step
    loop {
        let save = async {
            match save_at {
                Some(t) => Timer::at(t).await,
                None => core::future::pending().await,
            }
        };
        let cmd = match select3(btn.wait_for_low(), CMD.receive(), save).await {
            // Short press: next design (on release). Hold 2 s: sleep.
            Either3::First(()) => match select(btn.wait_for_high(), Timer::after_secs(2)).await {
                Either::First(()) => {
                    Timer::after_millis(50).await; // debounce
                    Cmd::Next
                }
                Either::Second(()) => Cmd::Sleep(0),
            },
            Either3::Second(cmd) => cmd,
            Either3::Third(()) => {
                save_brightness(&mut flash, brightness);
                save_at = None;
                log!("brightness: {brightness}% saved");
                continue;
            }
        };
        match cmd {
            Cmd::Next => {
                current = (current + 1) % GUARDS.len() as u8;
                show(&mut display, current).await;
            }
            Cmd::Pick(n) if (n as usize) < GUARDS.len() => {
                log!("ble: picked guard {n}");
                current = n as u8;
                show(&mut display, current).await;
            }
            Cmd::Pick(n) => {
                // Only the button wraps; a pick past the end is a mistake the client should see.
                log!("ble: no guard {n} (valid 0-{}), keeping guard {current}", GUARDS.len() - 1);
                display.draw(&|s| guards::no_guard(s, n)).await;
                // A press ends it early; the loop then takes that press as usual (next guard, or sleep if held).
                if let Either::First(()) = select(Timer::after_secs(3), btn.wait_for_low()).await {
                    show(&mut display, current).await;
                }
            }
            Cmd::Bright(b) => {
                brightness = b;
                set_backlight(&backlight, brightness);
                log!("brightness: {brightness}%");
                save_at = Some(Instant::now() + Duration::from_secs(2));
            }
            Cmd::Sleep(secs) => {
                // "Off": backlight and panel off, chip in light sleep until the button is pressed again.
                // Light, not deep: the C3 wakes from deep sleep only on GPIO0-5 and BOOT is GPIO9.
                log!("power: sleep");
                Timer::after_millis(5).await; // let the line leave USB before it suspends
                set_backlight(&backlight, 0);
                display.sleep().await;
                btn.wait_for_high().await; // otherwise the held button would wake us straight away
                Timer::after_millis(50).await;
                btn.listen(Event::LowLevel);
                btn.apply_wakeup_config(&WakeupConfig::default()).unwrap();
                if secs > 0 {
                    // serial 'z' (test/hardware/reconnect_time.py): also wake by timer, no button needed
                    lp.set_wakeup_deadline(esp_hal::time::Instant::now() + esp_hal::time::Duration::from_secs(secs));
                }
                if save_at.is_some() {
                    save_brightness(&mut flash, brightness); // the reset below would lose it
                }
                RESUME.store(RESUME_TAG | current as u32, Ordering::Relaxed);
                lp.sleep_light(RtcSleepConfig::default());
                // The BLE controller doesn't survive light sleep (it advertises nothing after) and only
                // shuts down when dropped, so wake by restarting; the panel keeps its image meanwhile.
                esp_hal::system::software_reset();
            }
        }
    }
}

async fn show(display: &mut Display, i: u8) {
    let t = Instant::now();
    display.draw(&GUARDS[i as usize]).await;
    log!("display: guard {i} drawn in {} ms", t.elapsed().as_millis());
    SHOWN.signal(i);
}

/// 'n' steps to the next guard (tools/drawtime.py); 'z' sleeps 5 s and wakes by timer (reconnect_time.py).
#[embassy_executor::task]
async fn serial(mut rx: UsbSerialJtagRx<'static, Async>) {
    let mut b = [0u8; 16];
    loop {
        let Ok(n) = rx.read(&mut b).await;
        for &c in &b[..n] {
            match c {
                b'n' => CMD.send(Cmd::Next).await,
                b'z' => CMD.send(Cmd::Sleep(5)).await,
                _ => {}
            }
        }
    }
}

// ---- Bluetooth LE: one service; any client (web app, nRF Connect) can pick a guard ----

const CONNS: usize = 3; // keeps advertising while connected, so a reloaded page still finds the board
const CHANNELS: usize = 2 * CONNS;
type Ctl = ExternalController<BleConnector<'static>, 4>;
type Stk = Stack<'static, Ctl, DefaultPacketPool>;

// GAP (name, appearance: 5) + GATT with Service Changed (4); see gap_and_gatt.
#[gatt_server(attribute_table_size = 9 + GuardService::ATTRIBUTE_COUNT)]
struct Server {
    svc: GuardService,
}

#[gatt_service(uuid = "9a49e65d-c7e4-4d64-a03d-061e26e9b26a")]
struct GuardService {
    /// Guard number: written to pick (text "2" or byte 0x02), read/notified to show the current one.
    #[characteristic(uuid = "6eeb6fbb-9458-47d6-9379-e73cab7dfa04", read, write, notify)]
    select: Vec<u8, 8>,
    /// Guard names, one per line, so the web app builds its buttons from the board.
    #[characteristic(uuid = "ab3ba50e-45cb-4145-ae7e-3066f7e872a8", read)]
    names: Vec<u8, 256>,
    /// Backlight percent as one raw byte, clamped to 10-100.
    #[characteristic(uuid = "38c7816a-72f6-40eb-b1ab-b01bb0136afc", read, write)]
    bright: u8,
    /// Log lines, as notifications.
    #[characteristic(uuid = "7c0bd792-ba8a-4791-be95-3c354c4c2fd2", notify)]
    log: Vec<u8, 160>,
    /// Firmware version (git tag, build.rs), so the web app can show what the board runs.
    #[characteristic(uuid = "30598302-24ed-4ff4-8c09-c9091182cab0", read)]
    version: Vec<u8, 48>,
}
const _: () = assert!(env!("CARDGUARD_VERSION").len() <= 48, "version string won't fit the version characteristic");

type Table = AttributeTable<'static, embassy_sync::blocking_mutex::raw::NoopRawMutex, _ATTRIBUTE_TABLE_SIZE>;

/// trouble's GapConfig leaves the GATT service empty. Without Service Changed, iOS caches the
/// characteristic list forever, so a firmware that adds one stays invisible until Bluetooth is toggled;
/// with it, a phone that isn't bonded must rediscover on every connection (Core spec Vol 3 Part G 2.5.2).
fn gap_and_gatt() -> Table {
    let mut table = Table::new();
    let mut gap = table.add_service(Service::new(service::GAP));
    gap.add_characteristic_ro(characteristic::DEVICE_NAME, "cardguard");
    gap.add_characteristic_ro(characteristic::APPEARANCE, &appearance::UNKNOWN);
    gap.build();
    let mut gatt = table.add_service(Service::new(service::GATT));
    // Affected handle range: all of them. Never indicated: the table only changes with a reflash.
    gatt.add_characteristic_small(characteristic::SERVICE_CHANGED, [CharacteristicProp::Indicate], [0x01u8, 0x00, 0xff, 0xff]).build();
    gatt.build();
    table
}

const BLE_STACK: usize = 12 * 1024; // from the heap

/// The BLE thread: its own executor, so the scheduler can run it ahead of the main thread.
extern "C" fn ble_thread(args: *mut c_void) {
    // SAFETY: args is the Box main leaked for this thread.
    let (connector, current, brightness) = *unsafe { Box::from_raw(args.cast::<(BleConnector<'static>, u8, u8)>()) };
    static EXECUTOR: StaticCell<Executor> = StaticCell::new();
    EXECUTOR.init(Executor::new()).run(|spawner| spawner.spawn(ble(connector, current, brightness, spawner).unwrap()));
}

/// Sets up the GATT server and BLE stack, starts advertising and notifications, then runs the stack.
#[embassy_executor::task]
async fn ble(connector: BleConnector<'static>, current: u8, brightness: u8, spawner: Spawner) {
    static SERVER: StaticCell<Server> = StaticCell::new();
    let server = SERVER.init(Server::new(gap_and_gatt()));
    let mut names: Vec<u8, 256> = Vec::new();
    for (i, n) in GUARD_NAMES.iter().enumerate() {
        if i > 0 {
            names.push(b'\n').unwrap();
        }
        names.extend_from_slice(n.as_bytes()).unwrap();
    }
    server.set(&server.svc.names, &names).unwrap();
    server.set(&server.svc.select, &Vec::from_slice(&[current]).unwrap()).unwrap();
    server.set(&server.svc.bright, &brightness).unwrap();
    server.set(&server.svc.version, &Vec::from_slice(env!("CARDGUARD_VERSION").as_bytes()).unwrap()).unwrap();

    static RESOURCES: StaticCell<HostResources<Ctl, DefaultPacketPool, CONNS, CHANNELS>> = StaticCell::new();
    static STACK: StaticCell<Stk> = StaticCell::new();
    let stack = STACK.init(trouble_host::new(ExternalController::new(connector), RESOURCES.init(HostResources::new())).build());
    spawner.spawn(advertiser(stack.peripheral(), server, spawner).unwrap());
    spawner.spawn(notifier(stack, server).unwrap());

    let mut runner = stack.runner();
    loop {
        if let Err(e) = runner.run().await {
            log!("ble: runner error {e:?}");
            Timer::after_secs(1).await;
        }
    }
}

/// Fast adverts (20-30 ms) after boot, wake and a disconnect, so a reconnecting phone finds the board
/// at once; slow (0.5-0.7 s, saves power) as soon as something connects, or after 30 s.
#[embassy_executor::task]
async fn advertiser(mut peripheral: Peripheral<'static, Ctl, DefaultPacketPool>, server: &'static Server<'static>, spawner: Spawner) {
    // Name only: name + the 128-bit service UUID overflow the 31-byte advert.
    let mut data = [0u8; 31];
    let len = AdStructure::encode_slice(
        &[AdStructure::Flags(LE_GENERAL_DISCOVERABLE | BR_EDR_NOT_SUPPORTED), AdStructure::CompleteLocalName(b"cardguard")],
        &mut data,
    )
    .unwrap();
    let mut mode = Adv::Fast;
    let mut since = Instant::now();
    loop {
        if OPEN.load(Ordering::Relaxed) as usize >= CONNS {
            mode = ADV.wait().await; // a disconnect frees a slot
            since = Instant::now();
            continue;
        }
        let (min, max) = if mode == Adv::Fast { (20, 30) } else { (500, 700) };
        let params = AdvertisementParameters {
            interval_min: Duration::from_millis(min),
            interval_max: Duration::from_millis(max),
            ..Default::default()
        };
        log!("ble: {} adverts", if mode == Adv::Fast { "fast" } else { "slow" });
        let adv = match peripheral.advertise(&params, Advertisement::ConnectableScannableUndirected { adv_data: &data[..len], scan_data: &[] }).await {
            Ok(a) => a,
            Err(e) => {
                log!("ble: advertising failed {e:?}");
                Timer::after_secs(1).await;
                continue;
            }
        };
        let fast_ends = async {
            match mode {
                Adv::Fast => Timer::at(since + Duration::from_secs(30)).await,
                Adv::Slow => core::future::pending().await,
            }
        };
        match select3(adv.accept(), ADV.wait(), fast_ends).await {
            Either3::First(Ok(conn)) => match conn.with_attribute_server(server) {
                Ok(conn) => {
                    OPEN.fetch_add(1, Ordering::Relaxed);
                    match connection(conn, server) {
                        Ok(task) => spawner.spawn(task),
                        Err(_) => {
                            log!("ble: no free connection task");
                            OPEN.fetch_sub(1, Ordering::Relaxed);
                        }
                    }
                    (mode, since) = (Adv::Slow, Instant::now());
                }
                Err(e) => log!("ble: attach failed {e:?}"),
            },
            Either3::First(Err(e)) => log!("ble: accept failed {e:?}"),
            Either3::Second(m) => (mode, since) = (m, Instant::now()), // dropping adv stops it; restart with m
            Either3::Third(()) => (mode, since) = (Adv::Slow, Instant::now()),
        }
    }
}

#[embassy_executor::task(pool_size = CONNS)]
async fn connection(conn: GattConnection<'static, 'static, DefaultPacketPool>, server: &'static Server<'static>) {
    let addr = conn.raw().peer_address();
    log!("ble: connected {addr:?} ({} open)", OPEN.load(Ordering::Relaxed));
    let svc = &server.svc;
    loop {
        match conn.next().await {
            GattConnectionEvent::Disconnected { reason } => {
                log!("ble: disconnected {addr:?}, reason {reason:?}");
                break;
            }
            GattConnectionEvent::Gatt { event } => {
                let (mut bright, mut subscribed) = (None, false);
                if let GattEvent::Write(e) = &event {
                    let h = e.handle();
                    e.with_data(|_, v| {
                        if h == svc.select.handle && !v.is_empty() {
                            // nRF Connect "Text" sends "2", the web app a raw byte 2
                            let n = if v[0].is_ascii_digit() { parse_u32(v) } else { v[0] as u32 };
                            let _ = CMD.try_send(Cmd::Pick(n));
                        } else if h == svc.bright.handle && !v.is_empty() {
                            // One raw byte; no text form: bytes 48-57 are the digits '0'-'9'.
                            bright = Some(v[0].clamp(10, 100));
                        } else if Some(h) == svc.log.cccd_handle {
                            let on = v.first().is_some_and(|b| b & 1 != 0);
                            subscribed = on;
                            log!("ble: {addr:?} {} the log", if on { "subscribed to" } else { "unsubscribed from" });
                        }
                    });
                }
                match event.accept() {
                    Ok(reply) => reply.send().await,
                    Err(e) => log!("ble: reply failed {e:?}"),
                }
                if let Some(b) = bright {
                    let _ = server.set(&svc.bright, &b); // reads return the applied value
                    let _ = CMD.try_send(Cmd::Bright(b));
                }
                if subscribed {
                    // Only to this client; may repeat lines a reconnecting client already saw.
                    for line in BACKLOG.lock(|b| b.borrow().clone()) {
                        let _ = svc.log.notify_raw(&conn, line.as_bytes(), false).await;
                    }
                }
            }
            _ => {}
        }
    }
    OPEN.fetch_sub(1, Ordering::Relaxed);
    ADV.signal(Adv::Fast);
}

/// Sends log lines and guard changes to every subscribed client.
#[embassy_executor::task]
async fn notifier(stack: &'static Stk, server: &'static Server<'static>) {
    loop {
        match select(LOG.receive(), SHOWN.wait()).await {
            Either::First(line) => {
                let _ = server.notify(stack, server.svc.log.handle, line.as_bytes()).await;
            }
            Either::Second(i) => {
                let _ = server.write_and_notify(stack, server.svc.select.handle, &[i][..]).await;
            }
        }
    }
}

fn parse_u32(v: &[u8]) -> u32 {
    v.iter().take_while(|c| c.is_ascii_digit()).fold(0u32, |n, c| n.saturating_mul(10).saturating_add((c - b'0') as u32))
}
