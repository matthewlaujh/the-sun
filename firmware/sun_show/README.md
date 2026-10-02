# sun_show

Standalone show firmware for the sun — **no web page, no Wi-Fi, no OTA** (USB uploads only). The Olimex ESP32-GATEWAY boots and plays over direct Ethernet to the PixLite E4-S Mk3 (Expanded, Art-Net, 23 universes, Rev Q wiring). The scenes take turns every 30 s with an 8 s crossfade (fast, for a transition space); each scene's speed and activity wander smoothly the whole time; everything is coloured through one saturated red/orange palette.

    USB ──▶ ESP32-GATEWAY ── Ethernet ──▶ PixLite E4-S 10.10.0.50 ── runs 1–8 ──▶ A, B1–B12, C1–C12

## Build / flash
    cd firmware/sun_show
    pio run -t upload            # over micro-USB (CH340); hold BUT1 if it won't enter the bootloader
    pio device monitor           # 115200: scene slots, the ring curve, then one line a minute with the live speed / p1 / fps

## What it plays
| Slot | Scene | Speed range | Param range |
|---|---|---|---|
| 0 | Solar surface | 0.50–1.60 | activity 0.30–0.85 — now with radial flare streaks that taper outward and a soft shoulder instead of a clamp |
| 1 | Neon fluid | 0.40–1.40 | zoom 0.35–0.70 · level ×3.6 in the core easing to ×1.0 at the rim (edges stay dark) |

**Wander**: every 5–10 s each scene picks a new random speed and parameter inside its range and glides there (time constant 3 s). Speed is integrated into the scene's phase, so a speed change never jumps the picture. **Scene length**: 30 s, then an 8 s crossfade (both scenes render during the fade). The scene that just left gets a fresh random start for next time.

## SETTINGS block (top of `src/main.cpp`)
- `MASTER_PCT` (100), `POWER_CAP` (1.00) — full brightness, no power cap. Safe for this show: with the red/orange palette, the W ceiling and the dark rings the whole dome peaks around 220 W (host measurement over the preview clip; worst case with every pixel at the palette's top is ~150 W per wedge PSU, each a 500 W RSP-500). Put the cap back to 0.90 if you ever switch to a white-heavy palette or a flat curve.
- `W_MAX` (90) — hard ceiling on the W channel so the white die never blows out on camera; R/G/B run to 255.
- `EQUAL_POWER` (1), `POWER_PEAK` (1.20), `POWER_GAMMA` (1.3) — equal-power palette: each palette entry is rescaled so the total drive R+G+B+W follows `POWER_PEAK·e^POWER_GAMMA`, i.e. depends only on energy, never on hue. Yellows (R+G) and white-tinged hot spots are no brighter than reds at the same energy, and brightness still varies inside the hot core. Higher gamma = more contrast between warm and hot.
- `SHOW[].level` / `.rimLevel` — per-scene energy gain in the core (inside `FOCUS_CORE_MM`) easing to `rimLevel` at the rim, so a scene can be lifted in the middle without lighting the edges (Neon fluid 3.6 → 1.0; raise rimLevel for brighter edges). The show starts on a random scene at power-up.
- `SHOULDER` (1.3) — soft shoulder on scene energy after ring gain / focus / breakthrough and before the palette, `e' = 1 − exp(−SHOULDER·e)`. The ×2.00 core lands at 0.93 instead of clipping flat at 1, so a hot cluster shades red → orange → white instead of being one block. Bigger = brighter and flatter.
- `FOCUS_CORE_MM` (240) / `FOCUS_RIM` (0.30) — core focus: scene energy is scaled by a radial envelope before the palette, full inside 240 mm and easing to 0.30 at the rim, so the hot white-tinged activity lives in the diffused centre and the pixelated outer rings sit calmer and deeper red. `FOCUS_RIM 1.0` switches it off.
- `BREAKTHROUGH` (1), `BREAK_LO` (0.60), `BREAK_HI` (0.85), `BREAK_GAIN` (1.00) — bright features punch through the dark rings. A pixel whose own scene energy (before focus and palette) is above `BREAK_LO` starts to be lifted; at `BREAK_HI` it gets the full `BREAK_GAIN` as if its ring were set to that, and the core-focus envelope is bypassed by the same amount. Everything below `BREAK_LO` keeps the ring shading, so a ray or filament runs out to the rim while the rest of those rings stay dark. It only lifts, never dims.
- `SCENE_MIN_SEC` / `SCENE_MAX_SEC` (30 / 30), `FADE_SECONDS` (8). Set min/max apart (e.g. 25 / 45) for varied scene lengths; the old slow show was 300 / 600.
- `WANDER_MIN_S` / `WANDER_MAX_S` (5 / 10), `WANDER_EASE_S` (3) — how often and how quickly things drift (old slow show: 20 / 60, 15).
- `SHOW[]` — scenes by name (full table at the bottom of `src/scenes.h`: 10 simulator patterns, 5 Pixel-rim scenes, 2 shaders) with speed and parameter ranges. Add a third line for a three-scene loop.
- `SUN_SAT` palette + `CONTRAST` (0.95) + `WARMTH` (−0.10) — the colour: red → orange → amber → yellow. Seven stops with brightness climbing all the way to the top (R saturates near 0.9); the hot end is carried by G (0.70 at the top) with only a little W (0.22, then `W_MAX`), so hot spots read yellow-orange rather than blowing out to a cold white in photos. More yellow: raise the G values in the top three stops; more red: lower them or push `WARMTH` negative. More negative warmth = redder; raise the G values in the last stops for more orange/amber.
- `SHADE[29]` — gain per pixel ring ×100, applied to the scene energy before the palette (so a dark ring goes deep red, not just dim) (ring 0 centre … ring 28 rim). Default is the curve you tuned on the rig-test page for the video look: `200` for the whole diffused core (rings 0–16, 0–480 mm) so it saturates white-hot, `164 141 112 85 54 28` tapering through the outer B rings (510–660 mm), then `6 15 6 14 6 15` for the pixelated C rings. `SHADE_USE_SAVED 1` ignores it and uses the curve saved on the board by gateway_rig_test instead. The **Sun Show Preview** page (firmware-rendered frames, live curve editor) gives you the `MASTER_PCT` / `W_MAX` / `SHADE[]` lines to paste here.
- `ETH_IP`, `PIXLITE_IP`, `ARTNET_UNI0`, AP name/password, `OTA_PASS` (must match `platformio.ini`).

## How it runs
Core 1: output task — scene → per pixel: energy × ring gain (lifted where a feature breaks through) × core focus → soft shoulder → palette → master × cap → W ceiling → 23 Art-Net packets, paced to 40 fps. Core 0: only the render helper, which takes an adaptive share of every frame (no radio, no server). Task watchdog 12 s on the output task: a hung frame reboots and the show restarts. Output is linear — the PixLite does gamma 2.2.

## Files
`platformio.ini` · `src/main.cpp` · `src/scenes.h` (patterns, palettes, sunify) · `src/fluid.h` (the two shaders) · `src/sun_map.h` (generated copy of `simulator/firmware/sun_map.h`, do not edit).
