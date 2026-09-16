# the sun — simulator

3D light simulator for a Ø1773 mm LED sun: 2,417 SK6812 RGBW pixels on 25 PCBs, inside the BIG LIGHT 9.1 assembly
(40×80 aluminium ring, wall mount, stand, 3 mm opal diffuser and an 8 mm cast-acrylic dome with a sanded outer face).

**Open `index.html`** — or the hosted copy on GitHub Pages once enabled. Everything is in the one file (Three.js is loaded from jsdelivr).

## What it does
- Renders every pixel at its KiCad position and lights it from the exact bytes that would go on the wire
  (pattern → master → cap 0.90 → gamma 2.2 → 8-bit RGBW), so what you see is what the PixLite E4-S receives.
- Physical diffuser model: cos⁴ screen law on the opal (≈56 mm equivalent height, ~50 % transmission) and on the
  dome (clear cast PMMA, 91 % LT @ 3 mm, outer face sanded), so blur grows from ~72 mm at the rim to ~280 mm at the centre.
- 52 patterns: *Rim ↔ dome* and *Dome studies* written for those optics, a few show patterns, and the four first-light tests.
- Output tab: power per PSU zone, frame stats, hover any LED for index / run / universe / channel / bytes.
- Live tab: point it at the ESP32-GATEWAY's WebSocket (raw 2417×4 frame or DDP packets) and it mirrors the real sun.
- Build tab: toggle assembly layers, exposure, room light; **Explode** slider pulls the layers apart.

## Control chain
Animation host (Olimex ESP32-GATEWAY, standalone AP + web server) → Ethernet → PixLite E4-S Mk3 in EXPANDED mode
→ 8 data runs → 25 boards. DDP on UDP :4048, byte offset = 4·i. `firmware/sun_map.h` carries the same map for C++.

## Layout
- `index.html` — the simulator (self-contained, 4.4 MB)
- `sun-map.json`, `sun-map.csv` — the authoritative pixel map
- `firmware/sun_map.h` — pixel map as C arrays (int16 mm×10) with universe/channel/zone helpers
- `source/` — `template.html` (the page before data is inlined), `sun_data.js` + `sun_model.js` (generated),
  `build_data.py` (map → data + header), `tess.py` / `export_model.py` (STEP → mesh pack), `assemble.py` (builds index.html)

## Rules baked in
Global brightness cap 0.90 · gamma 2.2 at output only · W channel for whites · hang rotation is view-only, pixels are never remapped.
