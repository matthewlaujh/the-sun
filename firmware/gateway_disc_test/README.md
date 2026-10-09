# gateway_disc_test

Bring-up test for the sun's control chain on the **centre disc only** (board A, 173 px, run 1, universes 1–2):

    phone / laptop ── Wi-Fi "SUN-DISC" ──▶ Olimex ESP32-GATEWAY ── Ethernet ──▶ PixLite E4-S ── run 1 ──▶ disc

## Build / flash (PlatformIO)
Layout: `platformio.ini` · `src/main.cpp` (firmware) · `src/index_html.h` (the web page, embedded in flash).
Platform is **pioarduino** (arduino-esp32 core 3.x — the stock `espressif32` platform is stuck on 2.x and has a different `ETH.begin` signature); PlatformIO fetches it plus ESPAsyncWebServer / AsyncTCP on the first build.

    cd firmware/gateway_disc_test
    pio run                      # compile only
    pio run -t upload            # flash over the GATEWAY's micro-USB (CH340), hold BUT1 if it won't enter bootloader
    pio device monitor           # 115200 baud

Verified: compiles clean on 2026-09-24 (core 3.3.12, ESPAsyncWebServer 3.12, AsyncTCP 3.5) — 1.09 MB flash / 53 kB RAM.

## Page over USB instead of Wi-Fi
Same page, over the USB cable, while the laptop stays on its normal Wi-Fi. Both links are always on — pick by which address you open.

    ~/.platformio/penv/Scripts/python.exe ../tools/usb_bridge.py     # from this folder (macOS/Linux: penv/bin/python)
    → http://localhost:8080                                          # header pill shows "usb ok"

Painting, fill, chase, fluid sliders and network settings all work; the live fluid mirror is capped at ~5 fps over USB (115200 baud). Ctrl+C the bridge before `pio run -t upload`. Details: `../gateway_rig_test/README.md`, protocol in `../shared/SunUsbLink/src/SunUsbLink.h`.

## Use
1. Join Wi-Fi **SUN-DISC** / password **sunshine26**, open **http://192.168.4.1** (or http://sun.local) — or use it over USB (above).
2. The header shows `eth 100M · 10.10.0.10` when the cable to the PixLite is live, and the output fps.
3. Paint / Erase / Pick, Fill all, Clear. ☀ is the master (boots at 30 %; ×0.90 power cap always applied).
4. **Chase test** walks one pixel through index order 0→172 — check it spirals out from the centre as in the map.
5. *Network / PixLite*: protocol (Art-Net / DDP), target IP, Art-Net universe for px 0, gateway Ethernet IP. **Find Art-Net nodes** sends an ArtPoll and lists what answers.

## Fluid shader mode
*Fluid shader* panel → **Fluid on**, pick a shader in the dropdown (**Neon fluid** · **Black-hole sunset** — simplex-fbm sky spiralling into a dark disc; for it the sliders mean size / spiral phase / pull x,y / pull strength / noise octaves). The ESP evaluates a per-pixel port of the GLSL (`src/fluid.h`) for every LED each frame and streams it. The page shows two discs side by side: **left = the ESP's actual computed frame** (~10 fps mirror, what the LEDs get), **right = the original shader in WebGL** at full resolution, same math and parameters, clock-synced to the ESP. Drag/hover on the right disc moves the focus point, wheel zooms — both discs (and the LEDs) follow. Sliders are live, no reflash: speed · zoom (was mouse-wheel z) · flow (phase, was mouse X) · focus x/y + glow (was the mouse hotspot) · brightness · saturation · white mix (how much of the RGB floor moves to the W die) · iterations (1–8, detail vs cost). Output is linear — gamma happens on the PixLite. `radius` (mm → |p| = 1) defaults to the 226 mm disc; set 886.5 for the full sun.

## PixLite settings to match (Advatek Assistant)
- Static IP **10.10.0.50 /24**, gateway blank (10.10.0.x avoids the 192.168.0.x clash with home Wi-Fi when configuring from a laptop). ESP is 10.10.0.10 and targets .50 by default; the target can be set to 10.10.0.255 (broadcast) on the page if the PixLite is on another .x.
- Protocol **Art-Net**, output/run 1 start universe **1**, pixel type SK6812 **RGBW**, 173 pixels on run 1 (EXPANDED mode as planned).
- Map: px *i* → universe 1 + ⌊i/128⌋, channel 1 + (i mod 128)·4 (from `simulator/sun-map.csv`).

## E4-S terminals in EXPANDED mode (verified 2026-09-24)
Expanded mode turns each physical output's two signal pins into two independent data runs — **the CLK pin is the odd run, the DATA pin the even run**:

| Run (plan) | Terminal | Boards |
|---|---|---|
| 1 | Output 1 **CLK** | A + B1 |
| 2 | Output 1 DATA | B2 B3 B4 |
| 3 | Output 2 CLK | B5 B6 B7 |
| 4 | Output 2 DATA | B8 B9 B10 |
| 5 | Output 3 CLK | B11 B12 C1 |
| 6 | Output 3 DATA | C5 → C4 → C3 → C2 |
| 7 | Output 4 CLK | C9 → C8 → C7 → C6 |
| 8 | Output 4 DATA | C12 → C11 → C10 |

Runs 6–8 run in descending wedge order (Rev Q, 2026-09-30 — shorter C→C hops; `sun_map.h` reflects it, PixLite patch unchanged). For the full-rig test firmware see `../gateway_rig_test/`.

The disc wired to Output 1 *DATA* stayed dark — it was listening on run 2 (universes 4–6). PixLite GND must be common with the LED PSU GND.

## Troubleshooting
- `eth link down`: cable / PixLite power; on very old GATEWAY revisions (A/B) the PHY has no power pin — change `ETH_POWER_PIN_` to -1.
- fps shows but no light: first check the DIN is on the **CLK** terminal (table above); then PixLite on a different subnet, wrong universe, or protocol mismatch — *Find Art-Net nodes* proves the network path.
- Serial monitor (115200) prints the AP, Ethernet link and config changes.
