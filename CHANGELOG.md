# Changelog

Notable changes to the sun's firmware and tools, newest first. Each entry names the branch it was made on.

## 2026-10-09 — USB page link + colour fix · `damon/usb-link` (Damon)

Tested on the bench rig (ESP32-GATEWAY → PixLite E4-S, full sun) over USB. The Wi-Fi path is unchanged in code but was not re-tested after these changes.

### Fixed
- **Solid colours all showed white** (`gateway_rig_test`). The page sends a colour as `"a":"255,0,0,0"`, but the firmware's tiny JSON reader (`jsonStr()`) stopped at the first comma, so only **R** arrived. G/B/W stayed at their boot value of 255, which made every preset (red, green, blue, …) look white, and the G/B/W sliders did nothing on the rig. Quoted values are now read to the closing quote; plain numbers are read as before.
- Same reader fixed in `gateway_disc_test`. Its paint colours travel as binary so the bug never showed there, but any future quoted value with a comma would have hit it.

### Added
- **The web page over the USB cable**, so the laptop can stay on its normal Wi-Fi (internet, editing, talking to collaborators) while tuning the rig. Previously the only way in was joining the ESP's SUN-DISC hotspot, which cuts the laptop's internet and was intermittently unresponsive.
  - `firmware/shared/SunUsbLink/` — small header-only library that carries the page's WebSocket messages (text and binary) and its HTTP GET/POST calls over the USB serial port, one line per message. The protocol is documented at the top of `SunUsbLink.h`. Status is only streamed while a laptop is listening; binary frames are dropped rather than queued when the line is busy, so USB can never stall a task.
  - `firmware/tools/usb_bridge.py` — laptop side. Finds the CH340, asks the ESP which firmware it runs, and serves that project's own page at `http://localhost:8080` (re-read on every refresh, so page edits need no flash). ESP log lines print in its terminal. Runs with PlatformIO's Python (pyserial included); reconnects by itself.
- `gateway_rig_test` and `gateway_disc_test` both use it. **USB and Wi-Fi are always on together** and feed the same handlers — choose by opening `http://localhost:8080` (USB) or `http://192.168.4.1` (SUN-DISC).
- The page's status pill now says `usb ok` or `ws ok`, so it's clear which link is in use.

### Changed
- `gateway_rig_test`: status JSON and the scene list are built by `statusJson()` / `scenesJson()` so Wi-Fi and USB share them; the housekeeping task also services the USB link. Free heap ≈ 59 kB with the link (≈ 60 kB before) — USB buffers sized to the minimum that fits a status line / mirror frame.
- `gateway_disc_test`: the live fluid mirror is capped at ~5 fps over USB (115200 baud); Wi-Fi keeps the full rate. LED output is unaffected either way.
- READMEs of both gateway builds and the HANDOVER file map describe the USB option.

### Not changed
- `sun_show` (production show) — untouched; still no radio, no page. A USB live-tuning page for it is planned for later.

### Usage
```
cd firmware
~/.platformio/penv/Scripts/python.exe tools/usb_bridge.py     # macOS/Linux: ~/.platformio/penv/bin/python
# open http://localhost:8080 — stop the bridge (Ctrl+C) before flashing over USB
```
