# gateway_rig_test

Channel / pixel test firmware for the **whole sun** — the Advatek-Assistant-style test patterns, but aware of the sun's map
(25 boards, 2,417 SK6812 RGBW, 8 data runs, 23 universes, Rev Q wiring):

    phone / laptop ── Wi-Fi "SUN-DISC" ──▶ Olimex ESP32-GATEWAY ── Ethernet ──▶ PixLite E4-S Mk3 (Expanded) ── runs 1–8 ──▶ A, B1–B12, C1–C12

`gateway_disc_test/` (centre disc only, paint + fluid shader) is unchanged; this project is its sibling and shares the same
network setup, PixLite patch and `sun_map.h`.

## Build / flash (PlatformIO)
Layout: `platformio.ini` · `src/main.cpp` (firmware) · `src/index_html.h` (web page, embedded in flash) · `src/scenes.h` (simulator patterns + palettes, ported to C) · `src/fluid.h` (the two GLSL-port shaders, shared with the disc test) · `src/sun_map.h` (generated — copy of `simulator/firmware/sun_map.h`, do not edit).

    cd firmware/gateway_rig_test
    pio run -e usb -e ota        # ONCE, WITH INTERNET — compiles both envs so everything is cached (see "Offline" below)
    pio run -e usb -t upload     # FIRST flash over micro-USB (CH340) — hold BUT1 if it won't enter the bootloader
    pio device monitor           # 115200

After that, no cable needed — **OTA over the ESP's own access point** (there is no Wi-Fi in the space, the ESP hosts it):

    # join Wi-Fi SUN-DISC / sunshine26 on the laptop
    pio run -e ota -t upload     # → 192.168.4.1:3232, auth sunshine26-ota

### Offline — the OTA upload always runs without internet
A laptop joined to SUN-DISC has no internet, at home or in the space. Everything the `ota` env needs must already be on disk:
- **Platform + toolchain + framework** (`~/.platformio/platforms/espressif32`, `~/.platformio/packages/*`) — shared by all projects, cached by the first build of any env.
- **Libraries** — PlatformIO keeps a *separate* copy per environment in `.pio/libdeps/usb/` and `.pio/libdeps/ota/`. Building only `usb` leaves `ota` wanting to download on its first run. So, once, with internet: `pio run -e usb -e ota` (compile only; no board needed). Check that `.pio/libdeps/ota/` contains `ESPAsyncWebServer` and `AsyncTCP`.
- **espota** is `tools/espota.py` inside the cached framework package — nothing extra.
- Dry run before going to the space: turn Wi-Fi off on the Mac (or join SUN-DISC) and run `pio run -e ota` — it must compile with no download attempts. If PlatformIO stalls on its own update check, `pio settings set check_platformio_interval 365` and `pio settings set check_prune_system_threshold 0` keep it quiet.
- Never `pio pkg update` / `pio upgrade` / change `lib_deps` versions right before a session — that re-downloads.

Notes on OTA:
- The output task is suspended for the upload (one black frame is sent first; the PixLite holds it), then the board reboots into the new slot. A failed upload leaves the old firmware running.
- macOS asks once to allow Python to accept incoming connections on the SUN-DISC network — say yes, or the upload hangs at "Waiting for device".
- The laptop can be on SUN-DISC and on the 10.10.0.x USB-Ethernet at the same time (different subnets).
- A build that never brings the AP up can't be fixed over OTA — keep the USB cable in the go-bag and use `-e usb`.
- Nothing here touches the Wi-Fi control plan: still AP-only, same SSID, same page at 192.168.4.1. `ArduinoOTA` simply listens on that interface (port 3232) alongside the web server.
- Partition table is `min_spiffs.csv` (two 1.9 MB app slots). Settings in NVS (protocol / IPs / universe) survive OTA; bump `CFG_VERSION` when changing the defaults.

## Use
1. Join **SUN-DISC** / **sunshine26**, open **http://192.168.4.1** (or http://sun.local). The map of all 2,417 pixels loads from the ESP (`/map.bin`, built from `sun_map.h`); the page mirrors the frame actually being sent (~7 fps). **face-down (bench)** flips the view to match the rig lying connector-side up (wedge 2 to the left of wedge 1, per wiring notes §9).
2. Header: `eth 100M · 10.10.0.10` when the PixLite cable is live, output fps, and the **estimated load on each PSU** from the frame being sent — D (disc, RSP-200), E (wedges 1–6, RSP-500), W (7–12, RSP-500). Amber above 70 %, red above 90 % of the PSU rating. Estimate = 0.40 W per pixel at full RGBW white (967 W total), scaled by the actual channel values; it doesn't know about the 5.15 V trim or PixLite/ESP draw.
3. **Select** what the pattern applies to: **All**, any of **run 1–8** (= E4-S terminals, see table below), and/or individual **boards**. Board step, Universe step and Spot ignore the selection.
4. **Mode**

   | Mode | What it does | Check |
   |---|---|---|
   | Off | everything dark | |
   | Solid | selection in colour A | colour / brightness per run or board, like Assistant's test colour |
   | Chase | one head per selected run walking in **wire order** (index order within the run), tail = length | direction and order of every board on every run in one go; a hop that's miswired shows as a jump |
   | Wipe | each run fills head→tail, then drains | pixel counts per run — the fill must end exactly at the run's last pixel |
   | Cycle | R · G · B · W · RGB white · RGBW white · A | colour order (GRBW on the PixLite) and that W is the fourth die |
   | Board step | one board at a time, A, B1 … B12, C1 … C12 | patch / which board sits where; label shows board · px count · run |
   | Universe step | one universe at a time, 1–23 | the PixLite patch — each universe must light exactly its 128 (or remainder) pixels; label shows universe, Art-Net universe, px range |
   | Ends | first px of every board = A, last = B | seams and counts at a glance (25 A dots, 25 B dots) |
   | Rainbow | hue by angle, rotating | the whole rig alive, low current |
   | Breathe | A ramps 0 → 100 % → 0 on the selection | PSU / thermal soak — watch the D/E/W pills and touch the nodes (wiring notes §8) |
   | Radial | ring expanding centre → rim | radial geometry across A→B→C |
   | Spot | one pixel — tap it on the map or type the index | single-pixel addressing; the info line gives board / run / universe / channel |

   | **Scene** | simulator patterns and shaders running on the ESP | see below |

   Step modes auto-advance at the **speed** slider's rate (6 s → 60 ms); tick **hold** to step manually with ◀ ▶. Speed is px/s ×3 for chase/wipe and 0.02–2 Hz for rainbow/breathe/radial.
5. **Scene mode** — the sun simulator's engine on the device (`scenes.h`, line-for-line ports of `simulator/source/template.html`):
   - 10 patterns — Solar surface, Breathing rings, Rotating rays, Radial pulse, Embers, Corona breath, Heartbeat, Aurora curtains, Photosphere, Spiral arms — plus the two shaders (Neon fluid, Black-hole sunset). Kept small on purpose: the ESP has ~150 kB of heap after Wi-Fi, and a first build with 21 patterns + float geometry copies starved the web server (blank page).
   - **palette**: the website's 16 ramps (Sun, Ember, Gold, White heat, Blue flame, Ice, Aurora, Mono white, Sky day, Melanopic, Dawn, Golden hour, Dusk, Blue hour, Night amber, Overcast) or *pattern's own colours*. Same `sunify` maths as the site: energy = 0.3R+0.5G+0.2B+0.9W → ramp lookup; **contrast** and **warmth** as on the site. Sky patterns and shaders default to their own colours.
   - **speed** (0.1–3×), and one pattern parameter (the website's p1 / first slider — label changes per scene). Colour A is the tint for Rotating rays and Radial pulse. Master ☀ and the 0.90 cap apply on top, output is linear (PixLite does gamma 2.2).
   - The fps pill shows `render N ms`: the whole 2,417-px frame is computed on core 1 each tick; heavy scenes (Black-hole sunset, Aurora curtains, Solar surface) will drop below 40 fps — motion speed is time-based so it stays correct.
   - Port check: every pattern was compared against the JS pixel-for-pixel; the non-noise ones match bit-for-bit, the noise ones match once the JS hash is made exact-integer (the browser loses bits above 2^53 in its hash, so its noise *seed* differs — same look, different field).
   - Not ported: the other simulator patterns, per-pattern extra sliders (defaults baked in), the "Live" tab. The build pill shows free heap — if it drops under ~40 kB the page mirror pauses; under ~20 kB expect trouble.
6. **Colour**: A and B (B is used by Ends). Swatches or RGBW sliders edit whichever is highlighted. ☀ is the master (boots at 30 %; ×0.90 power cap is always applied on top — never exceed the cap, the rig has no fuses).
7. *Network / PixLite*: protocol (Art-Net / DDP), target IP, the Art-Net universe that plan universe 1 maps to (leave 1 — PixLite start universes are 1-based and match), gateway Ethernet IP. **Find Art-Net nodes** sends an ArtPoll and lists what answers.

## Runs (E4-S Expanded: CLK terminal = odd run, DATA terminal = even run) — Rev Q, 2026-09-30
| Run | Terminal | Chain (wire order) | px | Universes |
|---|---|---|---|---|
| 1 | Out 1 CLK | A → B1 | 282 | 1–3 |
| 2 | Out 1 DATA | B2 → B3 → B4 | 327 | 4–6 |
| 3 | Out 2 CLK | B5 → B6 → B7 | 327 | 7–9 |
| 4 | Out 2 DATA | B8 → B9 → B10 | 327 | 10–12 |
| 5 | Out 3 CLK | B11 → B12 → C1 | 296 | 13–15 |
| 6 | Out 3 DATA | **C5 → C4 → C3 → C2** | 312 | 16–18 |
| 7 | Out 4 CLK | **C9 → C8 → C7 → C6** | 312 | 19–21 |
| 8 | Out 4 DATA | **C12 → C11 → C10** | 234 | 22–23 |

Runs 6–8 are in descending wedge order (shorter C→C hops); `sun_map.h` already reflects this, the PixLite patch is unchanged
(same px count and start universe per output). Seam links (cross a PSU domain, data + GND only): A→B1, B6→B7, C7→C6, B12→C1.

## PixLite settings to match (Advatek Assistant 3)
Static IP **10.10.0.50 /24**, Art-Net, Expanded ON, SK6812 RGBW, GRBW, Split Pixels Across Universes ON (128 px/universe),
1-based universes, gamma 2.2 + dither ON (the ESP sends linear). Outputs: 1: 282 px uni 1 · 2: 327 uni 4 · 3: 327 uni 7 · 4: 327 uni 10 · 5: 296 uni 13 · 6: 312 uni 16 · 7: 312 uni 19 · 8: 234 uni 22.

## Bring-up order suggestion
1. Cable to the PixLite only, nothing else powered: `eth 100M`, ~40 fps, **Find Art-Net nodes** shows 10.10.0.50.
2. One PSU, one wedge (wiring notes §8): Board step with hold → confirm each board lights where the map says. Chase on that run → direction and order. Wipe → the fill ends on the last pixel.
3. Universe step 1→23 with the full rig: each step lights one contiguous block, no gaps, no overlaps → the patch is right.
4. Ends → 25 A + 25 B dots; a missing B means a short count or a dead tail, a missing A means a dead head or a wrong seam.
5. Breathe on All at 30 % then 60 % for a few minutes: watch the D/E/W pills, touch every node and leg.

## Troubleshooting
- `eth link down`: cable / PixLite power; GATEWAY rev A/B has no PHY power pin → `ETH_POWER_PIN_ -1`.
- fps shows but a run is dark: DIN on the **CLK** terminal for odd runs (table above); then PixLite subnet / universe / protocol — *Find Art-Net nodes* proves the network path.
- A run lights but the wrong boards move first in Chase: the run's entry cable is on the wrong board's J_IN (wiring notes §3, E4-S run cables enter at A, B2, B5, B8, B11, C5, C9, C12).
- OTA "No response from device" / "Authentication failed": laptop not on SUN-DISC, firewall prompt not accepted, or `--auth` ≠ `OTA_PASS`.
- Serial monitor (115200, USB only) prints the AP, Ethernet link, OTA progress and config changes.
