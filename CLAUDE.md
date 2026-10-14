# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

"The Sun" is a Ø1.77 m LED disc: 2,417 SK6812 RGBW pixels on 25 PCBs under an opal diffuser and an acrylic dome, driven by an Olimex ESP32-GATEWAY → Art-Net → PixLite E4-S Mk3. **`HANDOVER.md` is the authoritative, as-built reference** (written for AI agents) — read the relevant section before changing the pixel map, the engine, power settings or the PixLite contract. `CHANGELOG.md` records branch-level changes; per-firmware READMEs document every tunable.

## Commands

There is no test suite or linter. "Testing" means building, flashing, and watching the rig / serial log.

Firmware (PlatformIO, pioarduino platform, Arduino core 3.x, board `esp32-gateway`; needs PlatformIO Core ≥ 6.2). On this Windows machine `pio` is not on PATH — use `~/.platformio/penv/Scripts/pio.exe`:
```
pio run -d firmware/sun_show -t upload                         # production show, USB only
pio run -d firmware/gateway_rig_test -e usb -t upload          # tuning build over USB
pio run -d firmware/gateway_rig_test -e ota -t upload          # over the air: laptop must be on Wi-Fi SUN-DISC (no internet there)
pio run -d firmware/gateway_rig_test -e usb -e ota             # once, with internet, so both envs' libs are cached for offline OTA
pio run -d firmware/gateway_disc_test -t upload                # centre-disc paint/fluid tool (single env)
pio device monitor                                             # 115200
```
Add `--upload-port COM3` if auto-detect fails; hold BUT1 during "Connecting…" if the bootloader won't enter. Upload speed stays 460800 (CH340 is flaky at 921600).

Page over USB instead of Wi-Fi (gateway_rig_test / gateway_disc_test): run `~/.platformio/penv/Scripts/python.exe firmware/tools/usb_bridge.py`, open http://localhost:8080. The bridge holds the serial port — **stop it before any USB upload/monitor**, restart after. It re-reads the page from `src/index_html.h` on every refresh, so page-only edits need no flash.

Simulator / map pipeline (Python 3, run in this order if `sun-map.json` changes):
```
python simulator/source/build_data.py     # sun-map.json → sun_data.js + sun_map.h (asserts pixels[i].i == i)
# copy simulator/firmware/sun_map.h into every firmware/*/src/; regenerate sun_show/src/relief_nb.h (generator not committed — HANDOVER §5.4)
python simulator/source/assemble.py       # template.html + sun_data.js + sun_model.js → source/out/sun-simulator.html → copy to simulator/index.html
cd simulator && python -m http.server     # view the simulator
```
Preset server: `cd server && npm install && SUN_ADMIN_KEY=test node server.js` — also defaults to port 8080, so it collides with usb_bridge (`--http 8081`).

## Architecture

**One pixel map, everything generated from it.** `simulator/sun-map.json` is canonical; `i` = global index = wire order = Art-Net order, and no software remaps it. `sun_map.h` (copied into each firmware's `src/`), `sun_data.js`, `simulator/index.html` and `relief_nb.h` are generated — never hand-edit them. `sun-map.pre-*` files are superseded; never load them.

**Two implementations of one animation engine, kept numerically matched.** Patterns are designed in JS (`PATTERNS` array in `simulator/source/template.html`, `fn(o, c)` fills the whole frame) and ported to C++ (`scenes.h`, `p_name(ib_t *o, const Ctx &c, int i0, int i1)` fills the half-open range `[i0, i1)` because two cores render disjoint halves — so no `static` per-frame state). Same constants and the same 32-bit noise hash (`Math.imul` in JS, `uint32_t` in C). Most scenes emit only an energy field (`E(o,i,v)`); `sunify` + a palette LUT colours it. Only `p1` is ported to C. **The two `scenes.h` copies have drifted — `sun_show`'s is current**; palette/sunify changes must go into `template.html` and the `scenes.h` you ship.

**Three firmwares, deliberately different:**
- `sun_show` — production. No radio, no page, no OTA (removed on purpose to eliminate network failure modes). Long post-chain in `main.cpp` (ring `SHADE[]`, breakthrough, core focus, shoulder, equal-power palette, W relief, `W_PER_G`, `W_MAX`), all `#define`s in its SETTINGS block, documented in its README.
- `gateway_rig_test` — tuning build: Wi-Fi AP "SUN-DISC" + async web page (Off/Solid/Scene, per-ring dome-shading tuner saved to NVS, config export/import) + ArduinoOTA. `src/index_html.h` is gzipped into `index_html_gz.h` by `tools/gen_html_gz.py` on every build (gitignored).
- `gateway_disc_test` — first-light fallback for the centre disc only (paint tool, fluid shader, live mirror).

**Task layout (gateway builds):** core 1 runs only the output task (render → shading × master × cap → 23 ArtDmx packets, paced to 40 fps with a guaranteed ≥ 5 ms sleep); core 0 runs Wi-Fi/lwIP, the housekeeping task (status, OTA, USB link, deferred NVS saves) and a low-priority render helper. Rendering must never block networking.

**Page ↔ ESP protocol.** The page talks JSON text + binary over a WebSocket, plus a few HTTP GET/POST routes. `firmware/shared/SunUsbLink` (header-only, pulled in via `symlink://../shared/SunUsbLink` in `lib_deps`) carries the exact same messages over USB serial; `firmware/tools/usb_bridge.py` turns them back into HTTP/SSE for the browser and auto-detects which firmware is plugged in. Wi-Fi and USB are always on together and feed the same handlers — when adding a page feature, register it once in the firmware's handlers and both links get it. ESP lines that don't start with `@` are logs. The firmware's `jsonStr()` is a minimal extractor for its own page's messages, not a JSON parser.

## Hard constraints

- **Output contract:** Art-Net unicast to 10.10.0.50, universes 1–23, 128 RGBW px per universe, **GRBW on the wire, linear values** (the PixLite applies gamma 2.2 + dither — never gamma in the ESP).
- **Power:** there are no fuses in the rig. Keep the 0.90 brightness cap; `sun_show`'s `POWER_CAP 1.00` is safe only because of its red/orange palette, `W_MAX` and dark outer rings. Power-check any new scene set before lowering the cap; don't test full-white fields at 100 %.
- **Heap is the scarce resource on the ESP32-GATEWAY, not CPU.** The Wi-Fi page died from heap exhaustion before (HANDOVER §5B); free heap should stay ≳ 60 kB (shown in the page header). No per-frame allocation; big tables go in flash or are `malloc`'d once at boot; static DRAM caps at ~124 kB.
- Angle `th` is degrees **anticlockwise from +x**; sectors/wedges are numbered **clockwise from the top**. Coordinates are mm, LED-side view, y up.
- `CFG_VERSION` in a gateway `main.cpp` must be bumped when its defaults change so stale NVS is discarded — but in `gateway_rig_test` that clears the whole `sunrig` namespace, **including the tuned dome-shading curve** (`sunrig/shr`). `sun_show` reads that same key when `SHADE_USE_SAVED 1`, so keep it a 29-byte array and export the config before bumping.

## Git

Upstream is `matthewlaujh/the-sun` (Matthew owns it). Damon works on `damon/<topic>` branches; don't commit to `main` directly.
