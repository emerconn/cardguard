// Web app (docs/app/index.html) connect / disconnect / reconnect states, against a fake DOM and a fake
// Web Bluetooth board. No dependencies: `node --test test/` (also runs in the Pages workflow before deploy).
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
      hidden: false, textContent: '', className: '', value: '60', scrollTop: 0, scrollHeight: 0, clientHeight: 0,
      childNodes: [], classes,
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
  // Fake board: connect() answers unless `reachable` is false, then it never settles (iOS, out of range).
  const board = { reachable: true, writes: [], pickerOpened: 0 };
  const device = {
    listeners: {}, addEventListener(t, f) { this.listeners[t] = f; },
    gatt: {
      connected: false,
      connect() {
        if (!board.reachable) return new Promise(() => {});
        this.connected = true;
        return Promise.resolve({ getPrimaryService: async () => ({ getCharacteristic: async (uuid) => characteristic(uuid) }) });
      },
      disconnect() { this.connected = false; device.listeners.gattserverdisconnected?.(); },
    },
  };
  const characteristic = (uuid) => ({
    addEventListener() {}, async startNotifications() {},
    async readValue() { return uuid.startsWith('ab3b') ? new TextEncoder().encode('A\nB\nC\nD') : { getUint8: () => 60 }; },
    async writeValue(v) { if (!device.gatt.connected) throw new Error('GATT not connected'); board.writes.push(v[0]); },
  });
  const navigator = { bluetooth: { requestDevice: async () => { board.pickerOpened++; return device; } } };
  const localStorage = { getItem: () => null, setItem() {} };
  const fastTimeout = (f, ms) => setTimeout(f, ms / 100);  // the page's 10 s reconnect timeout takes 100 ms here

  new Function('document', 'navigator', 'localStorage', 'setTimeout', script)(document, navigator, localStorage, fastTimeout);
  const tick = (ms = 0) => new Promise((r) => setTimeout(r, ms));
  return { els, document, device, board, tick, button: els.connect, status: els.status, guards: () => els.guards.childNodes };
}

async function connected() {
  const p = loadPage();
  await p.button.onclick();
  await p.tick();
  assert.equal(p.button.textContent, 'disconnect');
  return p;
}

test('connect shows Disconnect and the guard buttons', async () => {
  const p = await connected();
  assert.equal(p.board.pickerOpened, 1);
  assert.equal(p.guards().length, 4);
});

test('silent drop while away: coming back shows Connect, a guard tap reconnects', async () => {
  const p = await connected();
  p.device.gatt.connected = false;  // dropped while Bluefy was paused: no disconnect event
  p.document.listeners.visibilitychange();
  assert.equal(p.button.textContent, 'connect');
  assert.match(p.status.textContent, /dropped/);

  await p.guards()[2].onclick();
  assert.equal(p.button.textContent, 'disconnect');
  assert.equal(p.board.writes.at(-1), 2);
});

test('tapping a stale Disconnect never opens the picker', async () => {
  const p = await connected();
  p.device.gatt.connected = false;  // dropped, and the page has not noticed yet
  await p.button.onclick();
  assert.equal(p.board.pickerOpened, 1);
  assert.equal(p.button.textContent, 'connect');
  assert.equal(p.guards().length, 0);
  assert.match(p.status.textContent, /disconnected/);
});

test('reconnect to an unreachable board times out instead of hanging', async () => {
  const p = await connected();
  p.device.gatt.connected = false;
  p.board.reachable = false;
  await Promise.race([p.guards()[1].onclick(), p.tick(1000).then(() => assert.fail('reconnect hung'))]);
  assert.match(p.status.textContent, /timed out/);
  assert.equal(p.button.textContent, 'connect');
});

test('Disconnect while connected clears the controls', async () => {
  const p = await connected();
  await p.button.onclick();
  assert.equal(p.device.gatt.connected, false);
  assert.equal(p.button.textContent, 'connect');
  assert.equal(p.guards().length, 0);
});
