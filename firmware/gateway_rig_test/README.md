# gateway_rig_test

Controller firmware for the sun: Olimex ESP32-GATEWAY hosting Wi-Fi AP **SUN-DISC**, driving the PixLite E4-S Mk3 (Expanded) over direct Ethernet with Art-Net — 25 boards, 2,417 SK6812 RGBW, 8 runs, 23 universes (Rev Q wiring).

    phone / laptop ── Wi-Fi "SUN-DISC" ──▶ ESP32-GATEWAY ── Ethernet ──▶ PixLite E4-S ── runs 1–8 ──▶ A, B1–B12, C1–C12

Kept deliberately small: three modes (**Off · Solid · Scene**), the simulator's scenes and palettes running on the ESP, a per-ring **dome shading** tuner, OTA, config export/import. The pixel map / live mirror / channel testers from earlier builds are gone (git history ≤ `b82bf2d`, and `../gateway_disc_test/` still has the disc paint tool).

## Files
`platformio.ini` · `src/main.cpp` (firmware) · `src/index_html.h` (the page; gzipped into `src/index_html_gz.h` on every build by `tools/gen_html_gz.py`, generated file is gitignored) · `src/scenes.h` (patterns, palettes, sunify — C ports of `simulator/source/template.html`) · `src/fluid.h` (the two GLSL-port shaders) · `src/sun_map.h` (generated copy of `simulator/firmware/sun_map.h`, do not edit).

## Build / flash
    cd firmware/gateway_rig_test
    pio run -e usb -e ota        # ONCE, WITH INTERNET — caches both envs (PlatformIO keeps a separate .pio/libdeps/<env>/)
    pio run -e usb -t upload     # first flash over micro-USB (CH340); hold BUT1 if it won't enter the bootloader
    pio run -e ota -t upload     # afterwards: laptop joined to SUN-DISC → 192.168.4.1:3232, auth sunshine26-ota — works offline
    pio device monitor           # 115200 (USB only)

- A laptop on SUN-DISC has **no internet**, so everything must be cached beforehand (see above). Don't `pio pkg update` / `pio upgrade` before a session.
- OTA pauses the output (one black frame, PixLite holds it), flashes, reboots into the new slot; a failed upload leaves the old firmware running. macOS asks once to allow Python incoming connections — allow it or espota hangs at "Waiting for device".
- `-DUI_DARK=1` in `build_flags` → dark page theme (handy to see that an OTA landed; the header pill also shows the build time).
- `-DSINGLE_CORE_RENDER` → no render helper on core 0 (heavy scenes ~18 fps, Wi-Fi core untouched).

## Page (http://192.168.4.1)
- **Header**: Ethernet link, fps + scene render time, estimated load per PSU (D disc RSP-200 · E wedges 1–6 · W wedges 7–12; amber >70 %, red >90 %), build time · theme · free heap · uptime · last reset reason if abnormal.
- **Mode** Off / Solid / Scene. **Master** boots at 30 %; ×0.90 power cap is always applied on top (no fuses on the rig).
- **Scene**: 10 patterns (Solar surface, Breathing rings, Rotating rays, Radial pulse, Embers, Corona breath, Heartbeat, Aurora curtains, Photosphere, Spiral arms) + 2 shaders (Neon fluid, Black-hole sunset); palette (the website's 16 ramps or the pattern's own colours), speed, one pattern parameter, contrast, warmth. Output linear — the PixLite does gamma 2.2.
- **Colour**: the Solid colour, and the tint for Rotating rays / Radial pulse.
- **Dome shading**: gain per pixel ring (29 rings at 30 mm pitch; the outer 12 have their own slider, 0–480 mm share one), 0–2.00. While you move a slider that ring lights magenta on the rig (toggle). Saved on the ESP 1.5 s after the last change. Applies to everything sent.
- **Network / config**: PixLite IP, gateway Ethernet IP, Art-Net universe of plan universe 1 (leave 1). **Export config** downloads network + shading + current state as JSON; **Import config…** applies one.

## Architecture
- **Core 1**: the output task only — render → ring shading × master × cap → 23 Art-Net packets, paced to 40 fps with a guaranteed ≥5 ms sleep per frame.
- **Core 0**: Wi-Fi/lwIP, the housekeeping task (status every 1 s, OTA, deferred NVS saves), and a low-priority render helper that takes an adaptive share of each scene frame into its own buffer (one job in flight, late = skip; yields a tick mid-job for the task WDT). For 4 s after any page request the scene renders single-core so the page loads fast.
- Memory: pixel tables copied to RAM (~29 kB) so two cores don't stream flash through the single shared cache that also feeds Wi-Fi code; scene intent buffer is 16-bit fixed point (19 kB); page served gzipped (~6 kB) from flash.

## PixLite (Advatek Assistant 3) — unchanged
Static 10.10.0.50/24 · Art-Net · Expanded ON · SK6812 RGBW · GRBW · 128 px/universe · 1-based universes · gamma 2.2 + dither ON. Outputs: 1: 282 px uni 1 · 2: 327 uni 4 · 3: 327 uni 7 · 4: 327 uni 10 · 5: 296 uni 13 · 6: 312 uni 16 · 7: 312 uni 19 · 8: 234 uni 22. Terminals: CLK pin = odd run, DATA pin = even run.

| Run | Terminal | Chain (wire order) | px | Universes |
|---|---|---|---|---|
| 1 | Out 1 CLK | A → B1 | 282 | 1–3 |
| 2 | Out 1 DATA | B2 → B3 → B4 | 327 | 4–6 |
| 3 | Out 2 CLK | B5 → B6 → B7 | 327 | 7–9 |
| 4 | Out 2 DATA | B8 → B9 → B10 | 327 | 10–12 |
| 5 | Out 3 CLK | B11 → B12 → C1 | 296 | 13–15 |
| 6 | Out 3 DATA | C5 → C4 → C3 → C2 | 312 | 16–18 |
| 7 | Out 4 CLK | C9 → C8 → C7 → C6 | 312 | 19–21 |
| 8 | Out 4 DATA | C12 → C11 → C10 | 234 | 22–23 |

## Troubleshooting
- `eth link down`: cable / PixLite power; GATEWAY rev A/B has no PHY power pin → `ETH_POWER_PIN_ -1`.
- Page blank or slow: check the **heap** figure in the header (should sit well above 60 kB) and **last reset** — serial prints both at boot.
- OTA "No response" / "Authentication failed": laptop not on SUN-DISC, firewall prompt not accepted, or `--auth` ≠ `OTA_PASS`.
