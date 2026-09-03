# the sun — KiCad projects (draft 3)

Generated 2026-09-03 from the 2026-08-17 Studio DXF pack (sun-layout-dxf v2, on-line mounting).
Format: KiCad 8 s-expressions — KiCad 9 opens these and migrates them on save.

## Layout
- `sun-lib/` — shared symbols (`sun.kicad_sym`) + footprints (`sun.pretty/`)
- `sun-A/` — disc, 173 LEDs (172 + centre pixel), Ø448 mm
- `sun-B/` — wedge, 109 LEDs, r226→674, ×12
- `sun-C/` — wedge, 78 LEDs, r676→854, ×12
- per project: `.kicad_pro`, `.kicad_sch`, `.kicad_pcb`, `bom.csv`, `cpl.csv` (JLC format)

## Conventions (match Sun PCB Studio notes)
- Board coordinates: KiCad (x, y) = Studio/DXF (x, −y). No mirror: KiCad front = LED side = DXF view.
- **LED orientation is uniform-radial (draft 3.10)**: every LED's top edge (DIN/VDD side)
  faces AWAY from the sun centre, so the GND pad corner sits bottom-right radially on
  every LED, and every cap sits in the identical spot beside its LED's 5V pin
  (LED-local (−2.45, +4.40), long axis radial). Deviation from the Studio chain-rule pack.
- Data chain: LED pad2 DIN (+x local), pad4 DOUT (−x local); serpentine per Studio ROUTING layer.
- Nets: +5V (F.Cu pour), GND (B.Cu pour), D0 (J_IN→LED1), D1..D(n−1) chain, Dn (LEDn→J_OUT).
- WAGO 2601-3104 poles: **[5V | GND | GND | DATA]**, pads (1,2)(3,4)(5,6)(7,8); lever faces disc centre; **mounted on the BACK side** (same side as the standoffs, opposite the LEDs). Pole order runs mirrored vs a front mount — silk labels on the back read correctly from the wiring side.
- **WAGOs relocated clear of the mounting-plate strips** (30 mm skeleton members on the seam lines), deviation from the Studio pack:
  - **A**: the pair sits exactly diametrically opposite on the E–W line — J_IN at 0 deg (east), J_OUT at 180 deg (west), both at r = 192 mm, clear of the hub-spoke plates.
  - **B/C**: connectors are **docked parallel to their radial (wedge-cut) edge** — pole row along the edge direction, lever facing inboard. Pin-row centres 21.5 mm from the edge; courtyards ≥16 mm clear of the edge vs ~14 mm plate coverage.
- MH standoff footprints are true back-side footprints (placement side = Bottom).
- Standoffs: NPTH Ø4.22 + Ø6.2 pad on B.Cu (bottom reflow), net GND.
- A's DIN feed + C's DOUT return run on B.Cu through the GND pour (via at the LED end).

## Draft 3.15 changes (A connector centring + uniform standoff radius)
- **A's WAGO pair recentred**: pin centre moved from r195 to r199.06 so the
  connector BODY (which extends 10.4mm toward the lever/centre but only 2.3mm
  outward) sits exactly centred between rings 180 and 210; the pin centre was
  already the tangential midpoint of the two nearest LEDs. Still exactly
  opposite on the E-W axis. (Also fixed a sign bug in the placement search
  that tested the courtyard mirrored to the wrong side of the pins.)
- **A's six outer standoffs normalised to a common r=215** (slid inward along
  their radial skeleton members, angles unchanged): the Studio pack had five
  at ~218.5 with the copper moat ON the board edge and one at 215. Now every
  hole has the same ~5.9mm pad-to-edge and 3.5mm moat-to-edge margin.
  WARNING: the matching frame holes move 3.5mm inward on five spokes - update
  the skeleton drawing.

- **No trace under any LED or cap body — either layer** (3.16): LED/cap
  courtyards now keep out foreign traces on the BACK copper too, so the long
  back-layer runs (feeds/returns) weave between footprints instead of passing
  under them. Verified: zero back-copper crossings of any LED/cap body.
- **Pad exits run straight before turning** (3.16): exit stubs lengthened to
  2.5mm from pad centre (~1.75mm beyond the pad edge) and protected from the
  corner-smoothing pass; wrap/jog hooks land with a 1.5mm straight run into
  the DIN pad. Applies across all designs.
- **Fallback links smoothed into curves**: row-to-row jumps and dogleg hops
  (the paths the generic router produces near connectors and standoffs) now go
  through a clearance-validated corner-rounding pass, so they flow like the
  patterned links instead of showing chamfered elbows.

## Draft 3.14 changes (silk-free LED face)
- **All front silkscreen removed** except one mark per LED: the standard
  pin-1/GND TRIANGLE (0.7 mm, filled) tucked UNDER the package body with its
  tip pointing at pad 1 — visible on the bare board for orientation checks,
  completely hidden once the LED is soldered.
- Cap outlines, MH rings, and the front board title are gone; the JLC
  order-number placeholder ("JLCJLCJLCJLC") moved to the BACK silk so JLC's
  print can't land on the front. Board id stays on the back. WAGO pole labels
  (5V/G/G/D), lever mark and pin-1 dot stay on the back silk for hand-wiring.

## Draft 3.13 changes (A chain matches design pack 3)
- Board A's data chain re-ordered to match **sunleddesignpack 3**: the centre LED
  is now the FIRST pixel (universe 1, channel 1) and the serpentine runs in the
  pack-3 (mirrored) direction. LED refs 1..173 = pack3's LED1..173 exactly, so
  the pack's dmx-patch.csv / led-map.csv apply with no hand edits.
  Feed: J_IN -> centre on the back layer; return: LED173 (ring 210) -> J_OUT.
- Only output 1's boards shift in the pack's patch; outputs 2-4 are untouched.
- Mounting stays the BZXC / Ø4.22 layout (user choice; the pack's SMTSO scheme
  was NOT adopted) — centre standoff remains deleted (the centre LED sits there).

## Draft 3.12 changes (isolated mounting holes + A centre pixel)
- **Every mounting hole is now electrically ISOLATED**: the standoff's B.Cu solder
  pad has no net, and a copper-free moat (Ø11 mm keep-out ring, both layers) keeps
  the pours away — a screw scratching through the mask can't short 5V/GND.
  Traces additionally keep ≥3.0 mm from every hole edge (verified ≥2.8).
  Schematic MH pins carry no-connect markers. Standoffs float (not grounded).
- **Board A: centre mounting hole removed, LED173 added at the exact centre** as
  the LAST pixel of the chain (user choice: existing pixel addresses unchanged;
  the new pixel simply appends as A's pixel #173, channels 689-692 in A's map).
  LED172→LED173 runs as a 2-via back-side radial drop; the return LED173→J_OUT
  is a straight back-side shot due west. A now has 173 LEDs, 173 caps, 6 standoffs.

## Draft 3.11 changes (clearances + standoff keep-away)
- **Routing clearances raised** (user: safety margin): trace-to-copper routing target
  0.45 mm (DRC net class stays 0.35 — everything routes with ≥0.1 mm DRC margin),
  courtyard keep-out margin 0.5 mm, and **standoff holes keep all copper ≥2.4 mm
  from the hole edge** (verified ≥2.2 mm; JLC minimum is 0.3).
- No trace passes near a mounting hole any more: a wrap whose band would come
  near a standoff flips to the inward lane, and where the in/out alternation
  can't absorb that, the link takes a short, deterministic **2-via back-side
  jog** instead (A: 3 jogs, B: 1 jog at the row-98→99 jump, C: none).
- Ground stitching: not added — the B.Cu GND plane is the only ground plane
  (F.Cu carries the 5V pour), and the TWO GND vias at every LED (~350/220/160
  per board) already tie front copper to it at ~30 mm pitch everywhere.

## Draft 3.10 changes (uniform LED orientation)
- All LEDs re-oriented to the uniform-radial convention above (same footprint spots,
  the 90/180-degree branch of each rotation re-picked). CPL rotations updated.
- With uniform orientation, in the rings where the chain runs against the LED's
  DOUT→DIN direction the link cannot go straight. Those links use **front-side
  wraps, no vias**: alternating between an inward lane below the row (r_row − 3.75)
  and an outward band above the cap row (r_row + 6.6), as smooth polar arcs.
  Adjacent wraps alternate in/out so nothing crosses.
- The outward band **bulges smoothly around the NPTH standoffs** (4.15 mm keep-out
  envelope with a 2.5 mm ramp) where a mounting hole sits in its path.
- GND vias moved to uniform spots: LED GND via at LED-local (2.45, −2.55) tucked
  between pad 1 and the lane; cap GND via at (−1.15, 5.2) beside the cap's outer pad,
  under the band. Same spot on every LED.

## Routing rules (draft 3.9)
- **No trace enters a footprint it doesn't connect to on the part's own side**
  (LED/cap courtyards keep out front copper; WAGO courtyards keep out back copper).
- **Long runs go on the BACK layer** (feeds/returns beyond ~45 mm) and **never pass
  under any front pad** — every front pad projects a keep-out shadow onto B.Cu.
  Verified: zero courtyard crossings, zero B.Cu segments under front pads.
- **Long feeds/returns are smooth channel arcs**: instead of A* zig-zags they sweep
  a constant-radius polar arc mid-channel between LED rows (C's return runs down the
  middle of the gap between the two outer rows; A's feed/return follow the ring band),
  with cosine radius blending into the endpoints. Large-radius arc tracks (up to r 1500).
- WAGO data pole: pads 7-8 bridged; the feed/return ties in at pad 8 only
  (single junction at the pad, no mid-bridge branching).
- Vias may sit under the WAGO's back-side housing (courtyard keep-out applies to
  tracks, not tented vias); never under any front pad.
- LED part is now **SKC6812RGBW-NW (white face), LCSC C5348912** — electrically
  identical to the -B (black face) variant; pinout caveat unchanged.

## Stackup / fab (JLCPCB)
2-layer, 1.6 mm, **1 oz outer copper**, HASL or ENIG to taste.
1 oz is fine here: LED current flows in the near-full-face 5V/GND pours
(~0.5 mΩ/sq → tens of mV drop even at worst-case full white), and data traces
carry only signal current. With 1 oz, DO feed 5V/GND at BOTH connectors of
each board (poles are provided on both) and/or cap global brightness — the
limiting element is the WAGO pole (10 A UL), not the copper.
All tracks 0.6 mm; clearance 0.35 (board min 0.3); vias 0.8/0.4.

## First-open checklist (KiCad 9)
1. Open the SCHEMATIC once first (loads the netlist), then the PCB.
2. In the PCB run Tools → **Update PCB from Schematic** with **"Re-link footprints by reference"**
   checked — refs match 1:1, this permanently repairs any footprint↔symbol link mismatch
   (the draft-3 DRC "extra footprint" items). It should end reporting no changes.
3. Press **B** to fill zones (5V top pour, GND bottom pour ship unfilled).
4. Run DRC — expected clean. Courtyards are the standard full-size ones.
   Decoupling layout (deviation from the Studio pack, user-specified): each cap
   sits directly beside its LED's VDD pad (LED-local (-2.45, 4.40), long axis
   radial), 5V pad tied to the VDD pin with a short link; TWO GND vias per LED —
   one for the cap's GND pad, one for the LED's GND pad.
5. Save (migrates files to v9 format).

## Draft 3.1 changes (DRC fixes + curved traces)
- All data traces are now tangent **fillet arcs** (real KiCad arc tracks, r ≤ 2.5 mm) —
  no straight-line corners anywhere on the serpentine, feed, or return.
- MH standoff footprint is now a single plated pad (drill 4.22, Ø6.2 copper on B.Cu only):
  fixes the hole-clearance + solder-mask-bridge errors. Finished hole stays 4.22; the
  Ø4.09 boss fits a plated barrel.
- WAGO data pole: pads 7↔8 bridged with a short track (pole pins join inside the
  connector, but the board needs the copper) — fixes the unconnected-pad error.
- `sun.pretty` is generated from the same code as the embedded footprints
  (lib_footprint_mismatch warnings gone). Silk text ≥0.8 mm, back-side text mirrored,
  board labels moved into LED-free bands inside each outline.

## ⚠ Before ordering
- **LED pinout UNVERIFIED**: footprint assumes SKC6812RGBW-NW-B = 1 GND / 2 DIN / 3 VDD / 4 DOUT
  (per LCSC datasheet text extraction). VISUALLY confirm the package drawing corner map,
  and confirm JLC's CPL rotation for this part on the first article.
- **Standoff length**: stocked C20617225 is the 3 mm variant. The BSMTSO M3 family lists
  lengths to 30 mm with the same PCB interface — confirm the 30 mm orderable P/N with JLC/BZXC,
  or use the 3 mm nut + separate 30 mm spacers.
- WAGO 2601-3104 is hand-soldered (17.5 A IEC/10 A UL per pole).
- `cpl-reference.csv` is in Studio/DXF coordinates (y-up) for cross-checking only —
  for the actual JLC order, export the placement file from KiCad
  (File → Fabrication Outputs → Component Placement) so it matches the gerber origin.
