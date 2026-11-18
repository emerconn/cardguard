# Tests

Run from the repo root.

## Web app (no board needed)

```sh
node --test
```

`page.test.js` loads `docs/app/index.html` into a fake browser with a fake Bluetooth board and checks
connecting, highlights, frozen controls, automatic reconnects and disconnecting. No dependencies. The
`Tests` workflow runs it on every pull request (`web` check).

## Renderer (no board needed)

```sh
(cd guards && cargo test)
```

Unit tests next to the code in `guards/src/smooth.rs` (integer sin/cos, float-to-int casts). The
`Tests` workflow runs them and builds the firmware on every pull request (`firmware` check).

## Board (`hardware/`)

Needs the board on USB with the current firmware and no serial monitor open. One-time setup:

```sh
python3 -m venv .venv && .venv/bin/pip install bleak pyserial
```

| Script | Checks |
|---|---|
| `.venv/bin/python test/hardware/smoke.py` | After flashing: boot, free heap, BLE advert, guard names, valid and invalid picks, current guard (read and notify), brightness, BLE log |
| `.venv/bin/python test/hardware/reconnect_time.py [TRIALS]` | Sleeps the board over serial (`z`: 5 s, wakes by timer) and times the first advert and the reconnect after waking |

CI has no board, so run `smoke.py` locally for firmware changes and say so in the PR.
