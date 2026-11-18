// Checks the phone page (docs/app/index.html) connection states against a fake DOM + fake Web Bluetooth.
// Run: node tools/page_test.js   (no dependencies)
const fs = require('fs');
const html = fs.readFileSync(process.argv[2] || require('path').join(__dirname, '../docs/app/index.html'), 'utf8');
const script = html.split('<script>')[1].split('</script>')[0];

function el(id) {
  return {
    id, hidden: false, textContent: '', className: '', value: '60', scrollTop: 0, scrollHeight: 0, clientHeight: 0,
    childNodes: [], attrs: {}, classes: new Set(),
    classList: { toggle(c, on) { on ? this._s.add(c) : this._s.delete(c); }, _s: null },
    setAttribute(k, v) { this.attrs[k] = String(v); },
    append(t) { this.childNodes.push(t); }, appendChild(c) { this.childNodes.push(c); },
    replaceChildren() { this.childNodes = []; },
    get firstChild() { const self = this; return { remove() { self.childNodes.shift(); } }; },
  };
}
const els = {};
const document = {
  hidden: false, listeners: {},
  getElementById(id) { if (!els[id]) { els[id] = el(id); els[id].classList._s = els[id].classes; } return els[id]; },
  createElement() { const e = el('btn'); e.classList._s = e.classes; return e; },
  addEventListener(t, f) { this.listeners[t] = f; },
};
const localStorage = { getItem() { return null; }, setItem() {} };

// Fake board: connect() answers unless `reachable` is false (then it never settles, like iOS out of range).
const board = { reachable: true, writes: [] };
const chr = (uuid) => ({
  uuid, addEventListener() {}, async startNotifications() {},
  async readValue() { return uuid.startsWith('ab3b') ? new TextEncoder().encode('A\nB\nC\nD') : { getUint8: () => 60, buffer: null }; },
  async writeValue(v) { if (!device.gatt.connected) throw new Error('GATT not connected'); board.writes.push([...v]); },
});
const device = {
  listeners: {}, addEventListener(t, f) { this.listeners[t] = f; },
  gatt: {
    connected: false,
    connect() {
      if (!board.reachable) return new Promise(() => {});
      this.connected = true;
      return Promise.resolve({ getPrimaryService: async () => ({ getCharacteristic: async (u) => chr(u) }) });
    },
    disconnect() { this.connected = false; device.listeners.gattserverdisconnected?.(); },
  },
};
const navigator = { bluetooth: { requestDevice: async () => { picker++; return device; } } };
let picker = 0;
const TextDecoder = global.TextDecoder;
const setTimeoutFast = (f, ms) => setTimeout(f, ms / 100);  // 10 s timeout runs in 100 ms

new Function('document', 'navigator', 'localStorage', 'setTimeout', 'TextDecoder', script)(
  document, navigator, localStorage, setTimeoutFast, TextDecoder);

const btn = els.connect, status = els.status;
const tick = (ms = 0) => new Promise(r => setTimeout(r, ms));
let fails = 0;
const check = (what, ok) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) fails++; };

(async () => {
  await btn.onclick(); await tick();
  check('connect -> button says Disconnect', btn.textContent === 'Disconnect' && picker === 1);

  // Silent drop while Bluefy was paused: no gattserverdisconnected event.
  device.gatt.connected = false;
  document.hidden = false; document.listeners.visibilitychange(); await tick();
  check('back in the app after a silent drop -> button says Connect', btn.textContent === 'Connect');
  check('...status says the connection dropped', /dropped/.test(status.textContent));

  // Reconnect by tapping a guard.
  await els.guards.childNodes[2].onclick(); await tick();
  check('tap a guard -> reconnects and sends', btn.textContent === 'Disconnect' && board.writes.at(-1)[0] === 2);

  // Silent drop again, but this time tap the stale "Disconnect" (no visibilitychange).
  device.gatt.connected = false;
  const pickerBefore = picker;
  await btn.onclick(); await tick();
  check('stale Disconnect tap -> no picker, shows Connect, clears controls',
    picker === pickerBefore && btn.textContent === 'Connect' && els.guards.childNodes.length === 0 && /Disconnected/.test(status.textContent));

  // Connect again, then the board goes out of range and a guard tap must time out, not hang.
  await btn.onclick(); await tick();
  device.gatt.connected = false; board.reachable = false;
  const guard = els.guards.childNodes[1];
  await Promise.race([guard.onclick(), tick(1000).then(() => { throw new Error('hung'); })]).catch(e => check(`reconnect gives up (${e.message})`, false));
  check('unreachable reconnect -> times out with a message and shows Connect',
    /timed out/.test(status.textContent) && btn.textContent === 'Connect');

  // Normal Disconnect while really connected.
  board.reachable = true;
  await btn.onclick(); await tick();
  await btn.onclick(); await tick();
  check('real Disconnect -> Connect, controls cleared', btn.textContent === 'Connect' && els.guards.childNodes.length === 0);

  console.log(fails ? `${fails} FAILED` : 'all passed');
  process.exit(fails ? 1 : 0);
})();
