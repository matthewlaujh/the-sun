# sun_show

Standalone show firmware for the sun — **no web page**. The Olimex ESP32-GATEWAY boots and plays over direct Ethernet to the PixLite E4-S Mk3 (Expanded, Art-Net, 23 universes, Rev Q wiring). The scenes take turns for a random 5–10 minutes each with an 8 s crossfade; each scene's speed and activity wander smoothly the whole time; everything is coloured through one saturated red/orange palette. Wi-Fi **SUN-DISC** is hosted purely for OTA uploads.

    laptop ── Wi-Fi "SUN-DISC" (OTA only) ──▶ ESP32-GATEWAY ── Ethernet ──▶ PixLite E4-S 10.10.0.50 ── runs 1–8 ──▶ A, B1–B12, C1–C12

## Build / flash
    cd firmware/sun_show
    pio run -e usb -e ota        # ONCE, WITH INTERNET — caches both envs
    pio run -e usb -t upload     # first flash over micro-USB (CH340); hold BUT1 if it won't enter the bootloader
    pio run -e ota -t upload     # afterwards: laptop joined to SUN-DISC (sunshine26) → 192.168.4.1:3232, auth sunshine26-ota — works offline
    pio device monitor           # 115200 (USB only): scene slots, the ring curve, then one line a minute with the live speed / p1

OTA pauses the output (PixLite holds the last frame), flashes, reboots into the new slot; a failed upload resumes the show. macOS asks once to allow Python incoming connections — allow it or espota hangs at "Waiting for device".

## What it plays
| Slot | Scene | Speed range | Param range |
|---|---|---|---|
| 0 | Solar surface | 0.50–1.60 | activity 0.30–0.85 |
| 1 | Neon fluid | 0.40–1.40 | zoom 0.35–0.70 |

**Wander**: every 20–60 s each scene picks a new random speed and parameter inside its range and glides there (time constant 15 s). Speed is integrated into the scene's phase, so a speed change never jumps the picture. **Scene length**: random 5–10 min, then an 8 s crossfade (both scenes render during the fade). The scene that just left gets a fresh random start for next time.

## SETTINGS block (top of `src/main.cpp`)
- `MASTER_PCT` (70) — overall brightness; the ×0.90 power cap still applies on top.
- `SCENE_MIN_MINUTES` / `SCENE_MAX_MINUTES` (5 / 10), `FADE_SECONDS` (8).
- `WANDER_MIN_S` / `WANDER_MAX_S` (20 / 60), `WANDER_EASE_S` (15) — how often and how quickly things drift.
- `SHOW[]` — scenes by name (full table at the bottom of `src/scenes.h`: 10 simulator patterns, 5 Pixel-rim scenes, 2 shaders) with speed and parameter ranges. Add a third line for a three-scene loop.
- `SUN_SAT` palette + `CONTRAST` (1.10) + `WARMTH` (−0.25) — the colour. More negative warmth = redder; raise the G values in the last palette stops for more orange/amber.
- `SHADE[29]` — brightness per pixel ring ×100 (ring 0 centre … ring 28 rim): `100` centre → `80 60 40 22 10` taper → `50 25 40 20 …` alternating to the rim. `SHADE_USE_SAVED 1` ignores it and uses the curve saved on the board by gateway_rig_test instead.
- `ETH_IP`, `PIXLITE_IP`, `ARTNET_UNI0`, AP name/password, `OTA_PASS` (must match `platformio.ini`).

## How it runs
Core 1: output task — render → ring gain × master × cap → 23 Art-Net packets, paced to 40 fps. Core 0: Wi-Fi + OTA/housekeeping task, and a lowest-priority render helper that takes an adaptive share of every frame. Task watchdog 12 s on the output and housekeeping tasks (unsubscribed during an OTA upload); AP missing for 15 s → reboot, so there is always a way in once mounted. Output is linear — the PixLite does gamma 2.2.

## Files
`platformio.ini` · `src/main.cpp` · `src/scenes.h` (patterns, palettes, sunify) · `src/fluid.h` (the two shaders) · `src/sun_map.h` (generated copy of `simulator/firmware/sun_map.h`, do not edit).
