# the sun — JLCPCB ordering checklist (2026-08-30)

## Files per design (in `sun-X/production/`)
- Gerbers: `sun-X.zip` (upload as the PCB file)
- BOM: `bom.csv` (LCSC numbers filled in)
- CPL: `positions.csv`

## Order quantities
| Design | PCB qty | Assemble | LEDs/board |
|--------|---------|----------|------------|
| A disc | 5 | 2–5 | 173 |
| B wedge | 15 | 15 | 109 |
| C wedge | 15 | 15 | 78 |

Installed need: A×1, B×12, C×12 (2,417 px). Extra assembled boards = spares.

## PCB fab settings
- 2 layers, **1 oz outer copper** (default), 1.6 mm FR-4
- Surface finish: HASL or ENIG (either fine)
- NPTH + castellations: none; the Ø4.22 holes on the MH pads are PTH-pad-backed for the standoffs

## Assembly settings
- At checkout, tick **"Remove Order Number"** (no JLCJLCJLCJLC placeholder on the boards - we are not printing serials)
- **Double-sided assembly**: LEDs + caps on TOP, standoffs on BOTTOM
- When the BOM loads, **WAGO J_IN/J_OUT have no part — mark them "Do Not Place"** (hand-soldered THT)
- Check the component preview render: LED_SKC6812RGBW_5050 is a custom footprint, so verify LED pin-1 orientation before paying (GND corner = bottom-right radially; first-article check)

## Parts (assembled by JLC)
| Part | LCSC | Where | Per A/B/C | Total (5/15/15, all assembled) |
|------|------|-------|-----------|-------------------------------|
| SKC6812RGBW-NW LED (neutral white 3600–4500K) | C5348912 | top | 173/109/78 | 3,670 (+ attrition ~2%) |
| 100nF 0603 X7R (basic part) | C1591 | top | 173/109/78 | 3,670 |
| BZXC BSMTSOM330BTR M3 SMT nut-standoff, 3 mm | C20617225 | bottom | 6/8/4 | 210 |

If JLC's library stock of C5348912 is short, use "Order parts" to pre-load the reel(s) into your parts library first.

## Parts you buy elsewhere (hand-installed)
- **WAGO 2601-3104** 4-pole THT terminal block: 2/board → 50 installed, buy ~60 (Mouser / DigiKey / TME / Bürklin)
- **M3 × 30 mm male–female standoffs**: screw into the soldered SMT nuts to reach the skeleton past the 16.6 mm WAGOs → 150 installed, buy ~170, + M3 screws/washers
  - NOTE: the "30 mm BZXC standoff" in earlier notes was a misread — C20617225 is the 3 mm reflow nut that matches the Ø4.22/Ø6.2 footprint; the 30 mm riser is ordinary hardware threaded into it.

## Gotcha
NOTE: after the 2026-08-30 board edits (C WAGO labels moved to back silk) RE-EXPORT all three production folders in KiCad before uploading. Re-running Fabrication Toolkit regenerates `bom.csv` and blanks the LCSC column — re-add C1591 / C5348912 / C20617225 (or set an "LCSC Part #" field on the footprints in KiCad).
