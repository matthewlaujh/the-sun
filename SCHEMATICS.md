# Sun LED disc — schematics + boards

Three KiCad 10 projects, one per unique PCB design in `sun-pcb-kicad-pack`:

| Project | Design | Kind | Boards | LEDs | Parts | Sheet |
|---|---|---|---|---|---|---|
| `sun-pcb-design-A/` | A | centre disc | 1 | 172 | 355 | A0 |
| `sun-pcb-design-B/` | B | inner wedge | 12 | 109 | 227 | A1 |
| `sun-pcb-design-C/` | C | outer wedge | 12 | 78 | 164 | A1 |

Everything is generated from `sun-pcb-kicad-pack/` — `docs/design.json`, the
per-design placement CSVs and the outline DXFs — and verified against those same
sources.

```
python3 tools/gen_schematics.py     # .kicad_sch + .kicad_pro for all three
python3 tools/check_netlist.py      # netlist assertions on the schematics
$KIPY tools/gen_pcb.py              # .kicad_pcb for all three
$KIPY tools/check_pcb.py            # board assertions against the pack
# KIPY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3
```

`gen_pcb.py` needs KiCad's own interpreter because it drives `pcbnew` directly.
Two quirks worth knowing if you edit it: `ZONE_FILLER` segfaults unless a
`wx.App()` exists, and again unless the board has been saved and reloaded (a
board from `CreateEmptyBoard()` carries no PROJECT context).

## Topology (identical on all three boards)

```
J1.3 ──R1 100R──> D1 ─> LED1.DIN
                        LEDk.DOUT ─> D(k+1) ─> LED(k+1).DIN     (k = 1..n-1)
                        LEDn.DOUT ─> DATA_OUT ─> J2.3
+5V  : J1.1, J2.1, every LED.VDD(3), every C.1
GND  : J1.2, J2.2, every LED.VSS(1), every C.2
```

* `LED1..LEDn` and `C1..Cn` are in **placement-CSV order** — that order *is* the
  data path. Do not re-annotate.
* Each `Ck` is the 100 nF X7R 0603 belonging to `LEDk`; place it ≤ 2 mm from the
  package at layout time.
* Every LED carries its CSV row as hidden properties (`PosX_mm`, `PosY_mm`,
  `Rot_deg`, `Universe`, `Channel`) so the placement script has what it needs.
* Net classes `DATA` (0.25 mm) and `POWER` (2.5 mm) are defined in the
  `.kicad_pro`; pours still do the real current carrying.

## Connectors — J1 and J2 only

| Ref | Part | Pins | Purpose |
|---|---|---|---|
| J1 | `Conn_01x03` | 1 = +5V, 2 = GND, 3 = DATA_IN | power + data **in** |
| J2 | `Conn_01x03` | 1 = +5V, 2 = GND, 3 = DATA_OUT | power + data **out** |

These two 3-pin WAGO pigtails are the only connectors on every board. The
separate ≥ 10 A injection feeds that `schematic-requirements.md` §5 and
`electrical-requirements.md` called for have been removed. Footprints are
deliberately unassigned pending the connector choice.

Both connectors land on the same `+5V`/`GND` pours, so either end can feed the
board. **Feed each board its own PSU pair at J1.** A 3-pin WAGO can carry design
A's 13.8 A on its own, but it cannot carry a chain: passing power board-to-board
would put the sum of all downstream boards (up to ~8 boards / ~70 A on output 4)
through the first pigtail. Use J2's +5V/GND as a parallel link to the next board
only, never as its supply.

One bulk electrolytic sits at each connector (`C(n+1)` at J1, `C(n+2)` at J2),
since both are power entry points.

## Passive value standard

Every passive value reads **`<package>/<value>/<voltage>`** and is visible on
the sheet:

| Part | Value | Footprint |
|---|---|---|
| `C1..Cn` — per-LED decoupling | `0603/100nF/50V` | `C_0603_1608Metric` |
| `C(n+1)`, `C(n+2)` — bulk at J1/J2 | `D10/1000uF/10V` | (TBD) |
| distributed bulk (A: 6, B: 4, C: 3) | `1206/22uF/25V` | `C_1206_3216Metric` |
| `R1` — series data resistor | `0603/100R/50V` | `R_0603_1608Metric` |

`tools/check_netlist.py` enforces the pattern on every `C*`/`R*` part, so a
future edit that breaks the convention fails the check.

The LED value stays `SK6812` (matching the placement CSV `Val` column) and is
hidden in the array; the LED is not a passive, and its part number lives in the
`MPN`/`LCSC` properties.

## Board layout

2-layer FR4 1.6 mm, 2 oz outer copper, per `docs/pcb-design-guide.md`.

| | F.Cu — LED side | B.Cu — wire side |
|---|---|---|
| parts | `LED1..LEDn`, `C1..Cn`, `R1` | `J1`, `J2`, bulk `C(n+1)`/`C(n+2)`, distributed 22 µF |
| copper | data serpentine (0.25 mm) + **GND pour** | solid **+5V pour** |

* **Outline** comes from `design-X-outline.dxf`. Design A is a Ø446 mm circle;
  B and C are annular sectors rebuilt parametrically (the common arc centre is
  recovered from the two radii, exact to <0.001 mm, which avoids depending on
  the DXF's bulge-sign convention). Every LED lands exactly 13.0 mm inside its
  edge, matching the pack's `boardInsetMM`.
* **Placement** is `design-X-placement.csv` verbatim — verified to within 2 µm.
  The board's grid/aux origin is set to that CSV's (0,0), so KiCad's coordinate
  readout and the exported position files match the CSV directly.
* **Decoupling**: each `Ck` sits 0.8 mm off its LED package (`vinCap.gapMM`), on
  a side that alternates ring by ring.
* **Stitching**: every LED and every LED cap takes its +5V pad down to the plane
  through its own via — 352 / 224 / 161 vias per board.
* **Silkscreen**: LED and cap references are on `F.Fab`, not silk. 172 refs on a
  446 mm disc is unreadable on silk and produced ~360 overlap warnings.

### The one deviation from the pack

The placement CSV orients every LED to its ring tangent in a single fixed sense,
but the data chain serpentines — so on every other ring DIN/DOUT face *backwards*
along the chain. Taken literally, ~50 % of hops (75/171, 56/108, 41/77) would
have to wrap around the outside of each package, and that routing does not clean
up: two traces then have to cross every LED and whichever runs closer in gets cut
by the other's stub down to the pad row.

**These boards add 180° to those LEDs' rotation** so every hop is a short
straight run. The RGBW 5050 is square with a symmetric lens, so it is optically
identical, and the CPL export reflects the real board. Turned: 74/172 (A),
49/109 (B), 39/78 (C). Positions are untouched.

Set `SUN_FLIP_TO_CHAIN=0` to obey `Rot_deg` literally instead — it regenerates,
but wrapped hops leave DRC errors that need hand-routing.

## Verification

* `kicad-cli pcb drc` — **0 errors, 0 warnings, 0 unconnected items** on all
  three boards.
* `tools/check_pcb.py` — asserts LED positions against the placement CSV, that
  rotations differ only by 0° or 180°, side assignment for every part, a +5V via
  near every LED and cap, both pours present/filled on the right layers, and the
  outline inside the 460×500 mm panel envelope.
* All 355 / 227 / 164 footprints carry their schematic symbol UUID, so the
  boards are properly linked to the schematics.
* `kicad-cli sch erc` — **0 errors, 0 warnings** on all three.
* `tools/check_netlist.py` — exports the netlist and asserts, per design: the
  full `D1..Dn` chain node-by-node, `DATA_IN`/`DATA_OUT` membership, every LED
  and cap on `+5V`/`GND`, no stray nets, the exact part inventory, and the
  passive value standard.

## Open items

1. **J1/J2 footprint** is a placeholder: `Connector_Wago:Wago_734-133_1x03_
   P3.50mm_Vertical` (13.9 × 9.5 mm, 3 THT pads). Swap it for the real WAGO —
   note the 734 series is only rated 6 A/pole, well under design A's 13.8 A.
2. **Bulk cap** uses `Capacitor_THT:CP_Radial_D10.0mm_P5.00mm`; `D10` in the
   value string is a placeholder until the real 1000 µF part is chosen.
3. **LED footprint** is `LED_SMD:LED_OPSCO_SK6812_PLCC4_5.0x5.0mm_P3.1mm` (the
   OPSCO variant matching the supplied datasheet). Verify against the pad pitch
   of whichever LCSC part is actually ordered before Gerbers.
4. **Fiducials** (3 top / 2 bottom) are not placed yet.
5. **Mounting** is out of scope per the pack; nothing on these boards.
6. **Gerbers/BOM/CPL** not exported yet.

## Notes on the source pack

* The supplied datasheet is `C5380880.pdf` = OPSCO **SKC6812RGBX-XX-B**, while
  `component-requirements.md` names LCSC **C5160656**. Pinout is the same
  (1 = VSS/GND, 2 = DIN, 3 = VDD, 4 = DOUT — matches the `LED:SK6812` symbol),
  but the two LCSC numbers should be reconciled before ordering. The LCSC field
  on the LED symbols currently says `C5160656`, per the BOM doc.
* That datasheet specifies **8 mA per R/G/B channel and 16.5 mA for W**, i.e.
  ~40.5 mA full white, against the **80 mA** the pack budgets. If the datasheet
  is right the system worst case is ~98 A / 490 W rather than 193 A / 966 W —
  worth confirming with the actual part before sizing PSUs and feeder wire.
* Distributed 22 µF bulk works out to 6 / 4 / 3 per board (`round(n/30)`) = 90
  across the disc, against the pack's system-wide estimate of ≈ 81.
