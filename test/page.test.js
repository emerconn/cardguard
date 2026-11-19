// Web app (web/index.html) connect / disconnect / reconnect states, against a fake DOM and a fake
// Web Bluetooth board. No dependencies: `node --test` (runs on every PR in the Tests workflow).
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const html = fs.readFileSync(path.join(__dirname, '../web/index.html'), 'utf8');
const script = html.split('<script>')[1].split('</script>')[0];

// Loads the page script into a fresh fake browser; returns handles to drive and inspect it.
function loadPage({ bluetooth = true } = {}) {
  const els = {};
  const el = () => {
    const classes = new Set();
    return {
      hidden: false, disabled: false, textContent: '', className: '', value: '60', scrollTop: 0, scrollHeight: 0,
      clientHeight: 0, childNodes: [], classes,
      classList: { toggle: (c, on) => (on ? classes.add(c) : classes.delete(c)) },
      setAttribute() {}, append(t) { this.childNodes.push(t); }, appendChild(c) { this.childNodes.push(c); },
      replaceChildren() { this.childNodes = []; },
      get firstChild() { return { remove: () => this.childNodes.shift() }; },
    };
  };
  const document = {
    hidden: false, listeners: {},
    getElementById: (id) => (els[id] ??= el()),
    createElement: el,
    addEventListener(t, f) { this.listeners[t] = f; },
  };
  // Fake board: connect() answers unless `reachable` is false (asleep, out of range), then it never settles,
  // as on iOS. Its stored brightness reads 60.
  // failFast: connect rejects at once with a bare code, as Bluefy does ("2") for a sleeping board.
  const board = { reachable: true, failFast: false, asleep: false, pending: [], writes: [], pickerOpened: 0, current: 2 };
  const device = {
    listeners: {}, addEventListener(t, f) { this.listeners[t] = f; },
    gatt: {
      connected: false,
      connect() {
        if (board.failFast) return Promise.reject(new Error('2'));
        if (!board.reachable) return new Promise(() => {});
        this.connected = true;
        return Promise.resolve({ getPrimaryService: async () => ({ getCharacteristic: async (uuid) => characteristic(uuid) }) });
      },
      disconnect() { this.connected = false; device.listeners.gattserverdisconnected?.(); },
    },
  };
  // The board drops the link by itself (sleep, reset): the page gets the disconnect event.
  const boardDrops = () => {
    device.gatt.connected = false;
    device.listeners.gattserverdisconnected();
    board.pending.splice(0).forEach((no) => no(new Error('GATT Server is disconnected')));
  };
  // One object per characteristic, kept across reconnects like Web Bluetooth does. The guard one (6eeb…)
  // reads the board's current guard and notifies listeners when it changes.
  const chars = {};
  const characteristic = (uuid) => (chars[uuid] ??= {
    listeners: [],
    addEventListener(t, f) { this.listeners.push(f); }, async startNotifications() {},
    async readValue() {
      if (uuid.startsWith('ab3b')) return new TextEncoder().encode('A\nB\nC\nD');
      if (uuid.startsWith('3059')) return new TextEncoder().encode('v1.2.3');
      return { getUint8: () => (uuid.startsWith('6eeb') ? board.current : 60) };
    },
    async writeValue(v) {
      if (!device.gatt.connected) throw new Error('GATT not connected');
      // Board just fell asleep, link not dropped yet: the write hangs until the drop fails it.
      if (board.asleep) return new Promise((_, no) => board.pending.push(no));
      if (board.writeFails) throw new Error('GATT operation failed');  // link up, write refused
      board.writes.push(v[0]);
      if (uuid.startsWith('6eeb')) board.current = v[0];
    },
  });
  // BOOT pressed on the device: it shows the next guard and notifies.
  board.press = () => {
    board.current = (board.current + 1) % 4;
    const value = { getUint8: () => board.current };
    for (const f of characteristic('6eeb6fbb-9458-47d6-9379-e73cab7dfa04').listeners) f({ target: { value } });
  };
  // The board sends a log line ("<boot>.<n> text" from current firmware).
  board.log = (line) => {
    const value = new TextEncoder().encode(line);
    for (const f of characteristic('7c0bd792-ba8a-4791-be95-3c354c4c2fd2').listeners) f({ target: { value } });
  };
  const requestDevice = async () => {
    board.pickerOpened++;
    if (board.cancelPicker) throw Object.assign(new Error('cancelled'), { name: 'NotFoundError' });
    return device;
  };
  const navigator = bluetooth ? { bluetooth: { requestDevice } } : {};
  const localStorage = { getItem: () => null, setItem() {} };
  const fastTimeout = (f, ms) => setTimeout(f, ms / 1000);  // 10 s connect timeout -> 10 ms, 1 s retry -> 1 ms

  new Function('document', 'navigator', 'localStorage', 'setTimeout', script)(document, navigator, localStorage, fastTimeout);
  const tick = (ms = 0) => new Promise((r) => setTimeout(r, ms));
  const until = async (cond, ms = 2000) => {
    for (const end = Date.now() + ms; !cond(); await tick(5)) if (Date.now() > end) assert.fail('timed out waiting');
  };
  const frozen = () => els.bright.disabled && guards().every((b) => b.disabled);
  const live = () => !els.bright.disabled && guards().every((b) => !b.disabled);
  const guards = () => els.guards.childNodes;
  const logged = (re) => els.log.childNodes.some((t) => re.test(t));  // status details live in the debug log
  const highlighted = () => guards().flatMap((b, i) => (b.classes.has('on') ? [i] : []));
  return { els, document, device, board, boardDrops, tick, until, frozen, live, guards, highlighted, button: els.connect, logged };
}

async function connected() {
  const p = loadPage();
  const tap = p.button.onclick();
  assert.equal(p.button.textContent, 'connecting…');
  assert.ok(p.button.disabled);  // no second picker from a double tap
  await tap;
  assert.ok(!p.button.disabled);
  await p.tick();
  assert.equal(p.button.textContent, 'disconnect');
  assert.ok(p.live());
  return p;
}

test('connect shows disconnect and live guard buttons', async () => {
  const p = await connected();
  assert.equal(p.board.pickerOpened, 1);
  assert.equal(p.guards().length, 4);
});

test('debug mode shows the app version on its toggle and the board version on disconnect', async () => {
  const p = await connected();
  const debug = p.els.logToggle;
  assert.equal(debug.textContent, 'debug');
  assert.equal(p.button.textContent, 'disconnect');

  debug.onclick();  // on
  assert.equal(debug.textContent, 'dev');  // the release tag once deployed
  assert.equal(p.button.textContent, 'disconnect · v1.2.3');

  await p.button.onclick();  // disconnected: no version
  assert.equal(p.button.textContent, 'connect');
  await p.button.onclick();
  assert.equal(p.button.textContent, 'disconnect · v1.2.3');

  debug.onclick();  // off
  assert.equal(debug.textContent, 'debug');
  assert.equal(p.button.textContent, 'disconnect');
});

test('connect highlights the guard the board is showing', async () => {
  const p = await connected();  // the fake board starts on guard 2
  assert.deepEqual(p.highlighted(), [2]);
});

test('after sleep and reconnect, the highlight is the guard on the board', async () => {
  const p = await connected();
  await p.guards()[1].onclick();
  assert.deepEqual(p.highlighted(), [1]);
  p.board.reachable = false;
  p.boardDrops();
  p.board.reachable = true;
  await p.until(() => p.button.textContent === 'disconnect');
  assert.deepEqual(p.highlighted(), [1]);
});

test('BOOT pressed on the device moves the highlight', async () => {
  const p = await connected();
  p.board.press();
  assert.deepEqual(p.highlighted(), [3]);
  p.board.press();
  assert.deepEqual(p.highlighted(), [0]);
});

test('board drops (sleep, reset): controls freeze, then it reconnects by itself', async () => {
  const p = await connected();
  p.board.reachable = false;  // asleep
  p.boardDrops();
  assert.equal(p.button.textContent, 'connecting…');
  assert.ok(p.logged(/reconnecting/));
  assert.ok(p.frozen());

  await p.tick(20);  // a few retries while asleep: still frozen
  assert.ok(p.frozen());
  assert.equal(p.button.textContent, 'connecting…');  // timed-out tries cancel and fire drops; the label holds

  p.board.reachable = true;  // woken up
  await p.until(() => p.button.textContent === 'disconnect');
  assert.ok(p.logged(/reconnected/));
  assert.ok(p.live());
  assert.equal(p.board.pickerOpened, 1);  // no picker for a reconnect
});

test('a brightness dragged just as the board falls asleep snaps back to the board\'s level', async () => {
  const p = await connected();  // board brightness 60
  p.board.asleep = true;  // asleep, but the page has not seen the drop yet
  p.els.bright.value = '30';
  const sending = p.els.bright.oninput();  // hangs: nothing confirms it
  assert.equal(p.els.pct.textContent, '30%');  // live while dragging
  p.board.reachable = false;
  p.boardDrops();
  await sending;
  assert.ok(p.frozen());
  assert.equal(p.els.bright.value, 60);
  assert.equal(p.els.pct.textContent, '60%');
});

test('a brightness write that fails on a live link snaps the slider back', async () => {
  const p = await connected();  // board brightness 60
  p.board.writeFails = true;
  p.els.bright.value = '30';
  await p.els.bright.oninput();
  assert.ok(p.live());  // still connected
  assert.equal(p.els.bright.value, 60);
  assert.equal(p.els.pct.textContent, '60%');
  assert.ok(p.logged(/brightness failed/));
});

test('frozen controls send nothing', async () => {
  const p = await connected();
  p.board.reachable = false;
  p.boardDrops();
  const sent = p.board.writes.length;
  await p.guards()[2].onclick();
  p.els.bright.value = '30';
  await p.els.bright.oninput();
  assert.equal(p.board.writes.length, sent);
});

test('silent drop while away: coming back shows it and reconnects', async () => {
  const p = await connected();
  p.device.gatt.connected = false;  // dropped while Bluefy was paused: no disconnect event
  p.document.listeners.visibilitychange();
  assert.ok(p.logged(/reconnecting/));
  assert.equal(p.button.textContent, 'connecting…');
  await p.until(() => p.button.textContent === 'disconnect');
  assert.ok(p.live());
});

test('a write on a dead link counts as a drop; after reconnect the slider shows the board again', async () => {
  const p = await connected();
  p.device.gatt.connected = false;  // dropped, and the page has not noticed yet
  p.els.bright.value = '30';
  await p.els.bright.oninput();
  await p.until(() => p.button.textContent === 'disconnect');
  assert.equal(p.els.bright.value, 60);  // the board's real level, not the unsent 30
  assert.equal(p.els.pct.textContent, '60%');
});

test('retries give up after a while; connect then goes straight back without the picker', async () => {
  const p = await connected();
  p.board.reachable = false;
  p.boardDrops();
  await p.until(() => p.logged(/not found/), 10000);  // ~120 tries
  assert.equal(p.button.textContent, 'connect');
  assert.ok(p.frozen());

  p.board.reachable = true;
  await p.button.onclick();
  assert.equal(p.button.textContent, 'disconnect');
  assert.equal(p.board.pickerOpened, 1);
  assert.ok(p.live());
});

test('a failed connect tap keeps retrying, so waking the board brings it back', async () => {
  const p = await connected();
  p.board.failFast = true;  // asleep, and Bluefy rejects at once with "2"
  p.boardDrops();
  await p.button.onclick();
  assert.ok(p.logged(/connect failed: 2; retrying/));
  assert.equal(p.button.textContent, 'connecting…');

  await p.tick(20);
  p.board.failFast = false;  // woken up; iOS reconnects on its own
  await p.until(() => p.button.textContent === 'disconnect');
  assert.ok(p.logged(/reconnected/));
  assert.ok(p.live());
});

test('only one retry loop runs, however often connect is tapped', async () => {
  const p = await connected();
  p.board.failFast = true;
  p.boardDrops();
  for (let i = 0; i < 3; i++) await p.button.onclick();
  let attempts = 0;
  const connect = p.device.gatt.connect.bind(p.device.gatt);
  p.device.gatt.connect = () => { attempts++; return connect(); };
  await p.tick(12);  // ~12 retry periods of 1 ms: one loop makes ≤ ~12 tries, three loops ~36
  assert.ok(attempts <= 18, `${attempts} connect attempts: more than one loop is running`);
});

test('tapping a stale disconnect never opens the picker', async () => {
  const p = await connected();
  p.device.gatt.connected = false;  // dropped, and the page has not noticed yet
  await p.button.onclick();
  assert.equal(p.board.pickerOpened, 1);
  assert.equal(p.button.textContent, 'connect');
  assert.equal(p.guards().length, 0);
  assert.ok(p.logged(/disconnected/));
});

test('board log: a replay shows only unseen lines, a restarted board shows all', async () => {
  const p = await connected();
  const shown = () => p.els.log.childNodes.map((t) => t.split('  ')[1].trim()).filter((t) => !t.startsWith('web:'));
  p.board.log('a1f0.1 cardguard v1.2.0');
  p.board.log('a1f0.3 ble: subscribed');  // live line overtakes the replay
  p.board.log('a1f0.2 heap: 17 KB free');
  p.board.log('a1f0.1 cardguard v1.2.0');  // reconnect: the board replays its backlog
  p.board.log('a1f0.2 heap: 17 KB free');
  p.board.log('a1f0.4 ble: connected');
  p.board.log('07c3.1 cardguard v1.2.0');  // board restarted: numbers start over
  p.board.log('older firmware line');
  assert.deepEqual(shown(), [
    'cardguard v1.2.0', 'ble: subscribed', 'heap: 17 KB free', 'ble: connected', 'cardguard v1.2.0', 'older firmware line',
  ]);
});

test('without Web Bluetooth the button says to use Bluefy', () => {
  const p = loadPage({ bluetooth: false });
  assert.equal(p.button.textContent, 'open this page in Bluefy');
  assert.ok(p.button.disabled);
});

test('cancelling the picker goes back to connect', async () => {
  const p = loadPage();
  p.board.cancelPicker = true;
  await p.button.onclick();
  assert.equal(p.button.textContent, 'connect');
  assert.ok(!p.button.disabled);
});

test('disconnect clears the controls and does not reconnect', async () => {
  const p = await connected();
  await p.button.onclick();
  assert.equal(p.device.gatt.connected, false);
  assert.equal(p.guards().length, 0);
  await p.tick(30);
  assert.equal(p.button.textContent, 'connect');
  assert.equal(p.device.gatt.connected, false);
});
