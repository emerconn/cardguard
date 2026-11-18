// Web app (docs/app/index.html) connect / disconnect / reconnect states, against a fake DOM and a fake
// Web Bluetooth board. No dependencies: `node --test` (runs on every PR in the Tests workflow).
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const html = fs.readFileSync(path.join(__dirname, '../docs/app/index.html'), 'utf8');
const script = html.split('<script>')[1].split('</script>')[0];

// Loads the page script into a fresh fake browser; returns handles to drive and inspect it.
function loadPage() {
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
  const board = { reachable: true, failFast: false, writes: [], pickerOpened: 0 };
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
  const boardDrops = () => { device.gatt.connected = false; device.listeners.gattserverdisconnected(); };
  const characteristic = (uuid) => ({
    addEventListener() {}, async startNotifications() {},
    async readValue() { return uuid.startsWith('ab3b') ? new TextEncoder().encode('A\nB\nC\nD') : { getUint8: () => 60 }; },
    async writeValue(v) { if (!device.gatt.connected) throw new Error('GATT not connected'); board.writes.push(v[0]); },
  });
  const navigator = { bluetooth: { requestDevice: async () => { board.pickerOpened++; return device; } } };
  const localStorage = { getItem: () => null, setItem() {} };
  const fastTimeout = (f, ms) => setTimeout(f, ms / 1000);  // 10 s connect timeout -> 10 ms, 3 s retry -> 3 ms

  new Function('document', 'navigator', 'localStorage', 'setTimeout', script)(document, navigator, localStorage, fastTimeout);
  const tick = (ms = 0) => new Promise((r) => setTimeout(r, ms));
  const until = async (cond, ms = 2000) => {
    for (const end = Date.now() + ms; !cond(); await tick(5)) if (Date.now() > end) assert.fail('timed out waiting');
  };
  const frozen = () => els.bright.disabled && guards().every((b) => b.disabled);
  const live = () => !els.bright.disabled && guards().every((b) => !b.disabled);
  const guards = () => els.guards.childNodes;
  return { els, document, device, board, boardDrops, tick, until, frozen, live, guards, button: els.connect, status: els.status };
}

async function connected() {
  const p = loadPage();
  await p.button.onclick();
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

test('board drops (sleep, reset): controls freeze, then it reconnects by itself', async () => {
  const p = await connected();
  p.board.reachable = false;  // asleep
  p.boardDrops();
  assert.equal(p.button.textContent, 'connect');
  assert.match(p.status.textContent, /reconnecting/);
  assert.ok(p.frozen());

  await p.tick(20);  // a few retries while asleep: still frozen
  assert.ok(p.frozen());

  p.board.reachable = true;  // woken up
  await p.until(() => p.button.textContent === 'disconnect');
  assert.match(p.status.textContent, /reconnected/);
  assert.ok(p.live());
  assert.equal(p.board.pickerOpened, 1);  // no picker for a reconnect
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
  assert.match(p.status.textContent, /reconnecting/);
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
  await p.until(() => /not found/.test(p.status.textContent));
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
  assert.match(p.status.textContent, /not reachable; is it awake\? retrying/);
  assert.doesNotMatch(p.status.textContent, /\b2\b/);  // the raw code goes to the debug log, not the status
  assert.equal(p.button.textContent, 'connect');

  await p.tick(20);
  p.board.failFast = false;  // woken up; iOS reconnects on its own
  await p.until(() => p.button.textContent === 'disconnect');
  assert.match(p.status.textContent, /reconnected/);
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
  await p.tick(12);  // ~4 retry periods of 3 ms each
  assert.ok(attempts <= 6, `${attempts} connect attempts: more than one loop is running`);
});

test('tapping a stale disconnect never opens the picker', async () => {
  const p = await connected();
  p.device.gatt.connected = false;  // dropped, and the page has not noticed yet
  await p.button.onclick();
  assert.equal(p.board.pickerOpened, 1);
  assert.equal(p.button.textContent, 'connect');
  assert.equal(p.guards().length, 0);
  assert.match(p.status.textContent, /disconnected/);
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
