# Sun PCB Studio — KiCad pack

Disc Ø1773 mm · SK6812 RGBW · PixLite E4-S Mk3 · concentric rings, 12 wedges + centre disc · pitch 30 mm · 4.0 mm channels.

## Headline numbers

LEDs: 2416. PCBs: 25 in 3 unique designs. Universes: 20 (128 px max each, 94% filled). Controller outputs used: 4 (≤745 px, ≤6 universes each) → 1× PixLite E4-S Mk3. Power at full white: 966 W (193.3 A @ 5 V). Worst-chain frame rate: 38.7 fps (target 33.2).

DMX packing: shared/packed — universes run on across daisy-chained boards, so each output's boards must be wired in the chain order listed in boards.csv.

Mounting: intentionally out of scope in this studio — no holes or hardware on any board. A later revision adds SMT-standoff pads on the wire side without moving any LED.

## Checks at generation time

- ✓Universes packed across chained boards: 20 universes for 2,416 pixels (94% full, only 144 spare pixel slots, all at output ends). A universe may run across a board boundary, so each output's boards must be daisy-chained in the order given in boards.csv / dmx-patch.csv (DOUT → DIN).
- ✓All boards fit 460×500 mm (largest 446×458 mm).
- ✓3 unique PCB design(s) — mirrored/rotated duplicates counted once.
- ✓Fits one PixLite E4-S Mk3: 4/4 outputs, 20/24 universes.
- ✓Mounting intentionally out of scope: boards carry no holes or hardware, only the 5.5 mm LED-keepout rim. A later revision adds the SMT-standoff pads on the wire side without moving any LED — the rim geometry already leaves room for it.
- ✓Centre disc auto-sized by DMX + JLCPCB: Ø450 mm round board, 172 LEDs, 2 universe(s).
- ✓Open channel 4 mm wide at every seam (max 19 mm at this pitch) — board edges sit 13 mm from LED centres, so LED spacing across a seam is still exactly 30 mm.
- ✓No LED can sit off-board or on a seam: every board edge is pitch/2 from LED centres (rings-mode band cuts snap mid-way between LED rings) → 10.5 mm package→edge everywhere (≥ 3 mm keepout).
- ✓Biggest PCB: 172 px = 13.8 A full white → 2 feed(s) at 10 A. Bus width ≥ 2.2 mm per rail (2 oz, fed both ends, 10 °C rise) — use full copper pours on the wire side.

## File guide

docs/ — the four requirement documents plus design.json (machine-readable summary for the KiCad MCP agent). designs/ — per unique design: outline DXF (import to Edge.Cuts; REF layer marks DIN/DOUT), placement CSV (LED refs in chain order, board-local + global coords, rotation, universe/channel), routing SVG. led-map.csv — every LED in data-chain order. dmx-patch.csv — universe/board/output plan. boards.csv — physical board → design letter, position, rotation. config.json — reload via "Load config".
