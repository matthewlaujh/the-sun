#!/usr/bin/env python3
"""Generate the three KiCad schematics for the LED sun disc.

One project per unique PCB design (A = centre disc, B = inner wedge, C = outer
wedge).  Everything here is driven by sun-pcb-kicad-pack/docs/design.json and the
per-design placement CSVs, so the LED chain order in the schematic is exactly the
chain order the layout tool emitted.

Run:  python3 tools/gen_schematics.py
"""

from __future__ import annotations

import csv
import json
import math
import os
import re
import uuid
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PACK = REPO / "sun-pcb-kicad-pack"
KICAD_SYMS = Path(
    "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols"
)

U = 1.27  # KiCad default 50 mil grid; every coordinate below is a multiple of it

SCH_VERSION = "20260101"
NS = uuid.UUID("5b1c7f02-9d4a-5e63-9f21-6a3c0d8e1b47")  # stable uuid namespace

# ---------------------------------------------------------------- part choices
LED_LIB = "LED:SK6812"
LED_FP = "LED_SMD:LED_OPSCO_SK6812_PLCC4_5.0x5.0mm_P3.1mm"
LED_VAL = "SK6812"          # matches the Val column of the placement CSVs
LED_MPN = "SKC6812RGBW-NW"
LED_LCSC = "C5160656"
LED_DS = "https://www.opscoled.com/ SKC6812RGBX-XX-B"

# Passive values follow one house standard: <package>/<value>/<voltage>
C_LIB = "Device:C"
C_FP = "Capacitor_SMD:C_0603_1608Metric"
C_VAL = "0603/100nF/50V"
C_LCSC = "C1591"

CBULK_LIB = "Device:C_Polarized"
CBULK_FP = ""            # package pinned once the physical part is chosen
CBULK_VAL = "D10/1000uF/10V"
CDIST_LIB = "Device:C"
CDIST_FP = "Capacitor_SMD:C_1206_3216Metric"
CDIST_VAL = "1206/22uF/25V"

R_LIB = "Device:R"
R_FP = "Resistor_SMD:R_0603_1608Metric"
R_VAL = "0603/100R/50V"

CONN3_LIB = "Connector_Generic:Conn_01x03"
CONN2_LIB = "Connector_Generic:Conn_01x02"

# ------------------------------------------------------------ sheet geometry
COL_PITCH = 40 * U       # 50.80 mm — wide enough for the visible cap values
ROW_PITCH = 35 * U       # 44.45 mm
CAP_DX = 18 * U          # 22.86 mm — decoupling cap sits right of its LED
RAIL_DY = 12 * U         # 15.24 mm — +5V rail above / GND rail below each row
LBL_DX = 9 * U           # 11.43 mm — data label stub length from LED body
ARRAY_X0 = 60 * U        # 76.20 mm
# ref above / value below a vertical 2-pin part, both clear of its stub wire
PASSIVE_FIELDS = {"Reference": (1.6 * U, -3 * U, "left"),
                  "Value": (1.6 * U, 2.5 * U, "left")}
ARRAY_Y0 = 160 * U       # 203.20 mm
IFACE_Y = 60 * U         # 76.20 mm — anchor row of the interface block

PAPER = {"A": "A0", "B": "A1", "C": "A1"}
NCOL = {"A": 18, "B": 14, "C": 12}


# ============================================================ s-expr utilities
def _match(text: str, start: int) -> int:
    """Index just past the ')' closing the '(' at `start`."""
    depth = 0
    i = start
    in_str = False
    while True:
        ch = text[i]
        if in_str:
            if ch == "\\":
                i += 2
                continue
            if ch == '"':
                in_str = False
        elif ch == '"':
            in_str = True
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1


_lib_cache: dict[str, str] = {}


def lib_text(libname: str) -> str:
    if libname not in _lib_cache:
        _lib_cache[libname] = (KICAD_SYMS / f"{libname}.kicad_sym").read_text()
    return _lib_cache[libname]


def raw_symbol(lib_id: str) -> str:
    """Raw '(symbol "NAME" ...)' block out of the stock KiCad symbol library."""
    libname, name = lib_id.split(":", 1)
    text = lib_text(libname)
    start = text.find(f'\t(symbol "{name}"\n')
    if start < 0:
        raise KeyError(lib_id)
    return text[start : _match(text, start)]


def symbol_header(block: str) -> str:
    """The part of a symbol block before its first child (drawing) sub-symbol."""
    m = re.search(r'\n\t\t\(symbol "', block)
    return block[: m.start()] if m else block


def symbol_props(lib_id: str) -> dict[str, dict]:
    """Reference/Value/... field defaults (library coords) for a stock symbol."""
    head = symbol_header(raw_symbol(lib_id))
    out = {}
    for m in re.finditer(r'\t\t\(property "([^"]+)" "((?:[^"\\]|\\.)*)"\n', head):
        end = _match(head, head.rindex("(", 0, m.start(1)))
        body = head[m.start() : end]
        at = re.search(r"\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)", body)
        out[m.group(1)] = {
            "value": m.group(2),
            "x": float(at.group(1)),
            "y": float(at.group(2)),
            "angle": float(at.group(3)),
            "hide": "(hide yes)" in body,
        }
    return out


def symbol_pins(lib_id: str) -> list[str]:
    return re.findall(r'\(number "([^"]*)"', raw_symbol(lib_id))


def lib_symbol_entry(lib_id: str) -> str:
    """Stock symbol re-emitted for the schematic's (lib_symbols ...) section."""
    block = raw_symbol(lib_id)
    libname, name = lib_id.split(":", 1)
    block = block.replace(f'\t(symbol "{name}"\n', f'\t(symbol "{lib_id}"\n', 1)
    return "\n".join("\t" + ln if ln else ln for ln in block.split("\n"))


def uid(*parts) -> str:
    return str(uuid.uuid5(NS, "|".join(str(p) for p in parts)))


def n(v: float) -> str:
    return f"{round(v, 4):g}"


# ================================================================== emitters
class Sheet:
    def __init__(self, project: str, paper: str, title_block: str):
        self.project = project
        self.paper = paper
        self.title_block = title_block
        self.root_uuid = uid(project, "root")
        self.lib_ids: set[str] = set()
        self.body: list[str] = []
        self._pwr = 0
        self._flg = 0

    # -- primitives ------------------------------------------------------
    def wire(self, x1, y1, x2, y2):
        self.body.append(
            f"\t(wire\n\t\t(pts\n\t\t\t(xy {n(x1)} {n(y1)}) (xy {n(x2)} {n(y2)})\n\t\t)\n"
            f"\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)\n"
            f'\t\t(uuid "{uid(self.project, "w", x1, y1, x2, y2)}")\n\t)'
        )

    def junction(self, x, y):
        self.body.append(
            f"\t(junction\n\t\t(at {n(x)} {n(y)})\n\t\t(diameter 0)\n"
            f"\t\t(color 0 0 0 0)\n"
            f'\t\t(uuid "{uid(self.project, "j", x, y)}")\n\t)'
        )

    def label(self, text, x, y, angle=0):
        just = "right bottom" if angle in (180, 270) else "left bottom"
        self.body.append(
            f'\t(label "{text}"\n\t\t(at {n(x)} {n(y)} {n(angle)})\n'
            f"\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n"
            f"\t\t\t(justify {just})\n\t\t)\n"
            f'\t\t(uuid "{uid(self.project, "l", text, x, y)}")\n\t)'
        )

    def text(self, s, x, y, size=2.0, bold=False):
        b = "\n\t\t\t\t(bold yes)" if bold else ""
        esc = s.replace("\\", "\\\\").replace('"', '\\"')
        self.body.append(
            f'\t(text "{esc}"\n\t\t(at {n(x)} {n(y)} 0)\n'
            f"\t\t(effects\n\t\t\t(font\n\t\t\t\t(size {size} {size}){b}\n\t\t\t)\n"
            f"\t\t\t(justify left)\n\t\t)\n"
            f'\t\t(uuid "{uid(self.project, "t", s, x, y)}")\n\t)'
        )

    # -- symbols ---------------------------------------------------------
    def place(self, lib_id, x, y, ref, value=None, footprint=None,
              show=("Reference",), extra=None, datasheet=None, key=None,
              field_at=None):
        """Place one symbol instance at (x, y) with angle 0.

        field_at optionally overrides where a visible field lands, as
        {name: (dx, dy, justify)} in sheet mm relative to (x, y).
        """
        self.lib_ids.add(lib_id)
        props = symbol_props(lib_id)
        pins = symbol_pins(lib_id)
        key = key or ref
        su = uid(self.project, "sym", key)

        fields = []
        wanted = [
            ("Reference", ref),
            ("Value", value if value is not None else props["Value"]["value"]),
            ("Footprint", footprint if footprint is not None
             else props.get("Footprint", {}).get("value", "")),
            ("Datasheet", datasheet if datasheet is not None
             else props.get("Datasheet", {}).get("value", "")),
            ("Description", props.get("Description", {}).get("value", "")),
        ]
        for name, val in wanted:
            d = props.get(name, {"x": 0.0, "y": 0.0, "angle": 0.0, "hide": True})
            hide = name not in show
            px, py, angle, just = x + d["x"], y - d["y"], d["angle"], None
            if field_at and name in field_at:
                dx, dy, just = field_at[name]
                px, py, angle = x + dx, y + dy, 0
            esc = str(val).replace("\\", "\\\\").replace('"', '\\"')
            hidetag = "\n\t\t\t(hide yes)" if hide else ""
            justtag = f"\n\t\t\t\t(justify {just})" if just else ""
            fields.append(
                f'\t\t(property "{name}" "{esc}"\n'
                f'\t\t\t(at {n(px)} {n(py)} {n(angle)})\n'
                f"\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n"
                f"\t\t\t\t){justtag}{hidetag}\n\t\t\t)\n\t\t)"
            )
        for name, val in (extra or {}).items():
            esc = str(val).replace('"', '\\"')
            fields.append(
                f'\t\t(property "{name}" "{esc}"\n\t\t\t(at {n(x)} {n(y)} 0)\n'
                f"\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n"
                f"\t\t\t\t)\n\t\t\t\t(hide yes)\n\t\t\t)\n\t\t)"
            )

        pin_lines = "\n".join(
            f'\t\t(pin "{p}"\n\t\t\t(uuid "{uid(self.project, "pin", key, p)}")\n\t\t)'
            for p in pins
        )
        self.body.append(
            f'\t(symbol\n\t\t(lib_id "{lib_id}")\n\t\t(at {n(x)} {n(y)} 0)\n'
            f"\t\t(unit 1)\n\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n"
            f"\t\t(on_board yes)\n\t\t(dnp no)\n"
            f'\t\t(uuid "{su}")\n' + "\n".join(fields) + "\n" + pin_lines + "\n"
            f'\t\t(instances\n\t\t\t(project "{self.project}"\n'
            f'\t\t\t\t(path "/{self.root_uuid}"\n\t\t\t\t\t(reference "{ref}")\n'
            f"\t\t\t\t\t(unit 1)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)"
        )

    def p5v(self, x, y):
        self._pwr += 1
        self.place("power:+5V", x, y, f"#PWR{self._pwr:03d}", show=(),
                   key=f"pwr5v{self._pwr}")

    def gnd(self, x, y):
        self._pwr += 1
        self.place("power:GND", x, y, f"#PWR{self._pwr:03d}", show=(),
                   key=f"pwrgnd{self._pwr}")

    def pwr_flag(self, x, y):
        self._flg += 1
        self.place("power:PWR_FLAG", x, y, f"#FLG{self._flg:03d}", show=(),
                   key=f"flg{self._flg}")

    # -- output ----------------------------------------------------------
    def render(self) -> str:
        libs = "\n".join(lib_symbol_entry(i) for i in sorted(self.lib_ids))
        return (
            f"(kicad_sch\n\t(version {SCH_VERSION})\n\t(generator \"eeschema\")\n"
            f"\t(generator_version \"10.0\")\n\n"
            f'\t(uuid "{self.root_uuid}")\n\n'
            f'\t(paper "{self.paper}")\n\n'
            f"{self.title_block}\n\n"
            f"\t(lib_symbols\n{libs}\n\t)\n\n"
            + "\n".join(self.body)
            + "\n\n\t(sheet_instances\n\t\t(path \"/\"\n\t\t\t(page \"1\")\n\t\t)\n\t)\n"
            "\t(embedded_fonts no)\n)\n"
        )


# ==================================================================== design
def title_block(letter: str, meta: dict) -> str:
    return (
        "\t(title_block\n"
        f'\t\t(title "Sun LED Disc — Design {letter} '
        f'({meta["kind"]}, {meta["leds"]} px)")\n'
        '\t\t(date "2026-08-16")\n'
        '\t\t(rev "A")\n'
        '\t\t(company "the-sun")\n'
        f'\t\t(comment 1 "SK6812 RGBW pixel chain, 5 V, pass-through data — '
        f'{meta["boardsUsing"]}x board(s) of this design")\n'
        '\t\t(comment 2 "Generated by tools/gen_schematics.py from '
        'sun-pcb-kicad-pack/docs/design.json + placement CSV")\n'
        f'\t\t(comment 3 "Chain order = design-{letter}-placement.csv order. '
        'DO NOT re-annotate.")\n'
        "\t)"
    )


def build(letter: str, meta: dict, rows: list[dict]) -> Sheet:
    project = f"sun-pcb-design-{letter}"
    nled = len(rows)
    assert nled == meta["leds"], (letter, nled, meta["leds"])
    # J1 and J2 are the only connectors on the board, so they are also the only
    # power entry points — one bulk electrolytic sits at each of them.
    nbulk = 2
    ndist = max(1, round(nled / 30))

    sh = Sheet(project, PAPER[letter], title_block(letter, meta))

    # -------------------------------------------------- header text block
    sh.text(f"DESIGN {letter} — {meta['kind']}, {nled} x SK6812 RGBW, "
            f"used on {meta['boardsUsing']} board(s)", 30 * U, 12 * U, 3.5, True)
    spans = ", ".join(
        f"U{s['universe']} ch{s['chStart']}-{s['chEnd']} ({s['px']} px)"
        for s in meta["universeSpans"]
    )
    notes = [
        f"Data chain: J1.DATA_IN -> R1 (100R) -> LED1.DIN; LEDk.DOUT -> "
        f"LEDk+1.DIN; LED{nled}.DOUT -> J2.DATA_OUT.",
        f"Reference designators LED1..LED{nled} / C1..C{nled} are the chain "
        f"order from designs/design-{letter}-placement.csv — do not re-annotate.",
        f"Universe spans on the representative board: {spans}. Universes run on "
        f"across the daisy chain — see dmx-patch.csv.",
        f"Full white: {meta['ampsFullWhite']:.2f} A @ 5 V. J1 and J2 are the "
        f"ONLY connectors — both land on the same +5V/GND pours, so the board "
        f"can be fed from either end. Feed each board its own PSU pair at J1; "
        f"do not chain {meta['ampsFullWhite']:.2f} A of board current through "
        f"an upstream board's pigtail.",
        f"C1..C{nled} = one 0603/100nF/50V X7R per LED, <= 2 mm from its "
        f"package. Passive values read <package>/<value>/<voltage>.",
        "J1/J2 pinout: pin 1 = +5V, pin 2 = GND, pin 3 = DATA. Boards "
        "daisy-chain J2 (DOUT) -> next board J1 (DIN) per boards.csv.",
        "No level shifter: the PixLite E4-S Mk3 drives 5 V data directly. "
        "Keep board-to-board pigtails < 150 mm and pair DATA with GND.",
    ]
    for i, t in enumerate(notes):
        sh.text("- " + t, 30 * U, (17 + 4 * i) * U, 1.8)

    # ---------------------------------------------------------- J1 / DIN
    j1x, j1y = 48 * U, IFACE_Y
    sh.place(CONN3_LIB, j1x, j1y, "J1", value="Conn_01x03",
             show=("Reference", "Value"),
             extra={"Function": "J_DIN", "Pinout": "1=+5V 2=GND 3=DATA_IN",
                    "Note": "3-pin WAGO — power + data in"})
    p1, p2, p3 = (j1x - 5.08, j1y - 2.54), (j1x - 5.08, j1y), (j1x - 5.08, j1y + 2.54)
    sh.wire(*p1, 35 * U, p1[1]); sh.wire(35 * U, p1[1], 35 * U, 49 * U)
    sh.p5v(35 * U, 49 * U)
    sh.wire(*p2, 29 * U, p2[1]); sh.wire(29 * U, p2[1], 29 * U, 71 * U)
    sh.gnd(29 * U, 71 * U)
    sh.wire(*p3, 40 * U, p3[1]); sh.wire(40 * U, p3[1], 40 * U, 82 * U)
    sh.label("DATA_IN", 40 * U, 79 * U, 90)
    sh.text("J1  J_DIN  (1:+5V  2:GND  3:DATA IN)  — power entry", 44 * U,
            54 * U, 1.8)

    # bulk electrolytic at the J1 power entry
    cx1 = 92 * U
    sh.place(CBULK_LIB, cx1, IFACE_Y, f"C{nled + 1}", value=CBULK_VAL,
             footprint=CBULK_FP, show=("Reference", "Value"),
             field_at=PASSIVE_FIELDS)
    sh.wire(cx1, IFACE_Y - 3 * U, cx1, 49 * U); sh.p5v(cx1, 49 * U)
    sh.wire(cx1, IFACE_Y + 3 * U, cx1, 73 * U); sh.gnd(cx1, 73 * U)
    sh.text("bulk at J1", cx1 + 3 * U, IFACE_Y + 6 * U, 1.5)

    # R1 — 100 R series termination, sits at LED1 on the PCB
    sh.place(R_LIB, 40 * U, 85 * U, "R1", value=R_VAL, footprint=R_FP,
             show=("Reference", "Value"),
             field_at={"Reference": (2.5 * U, -2 * U, "left"),
                       "Value": (2.5 * U, 1 * U, "left")})
    sh.wire(40 * U, 88 * U, 40 * U, 92 * U)
    sh.label("D1", 40 * U, 92 * U, 270)
    sh.text("place at LED1", 42.5 * U, 89 * U, 1.5)

    # --------------------------------------------------------- J2 / DOUT
    j2x, j2y = 48 * U, 115 * U
    sh.place(CONN3_LIB, j2x, j2y, "J2", value="Conn_01x03",
             show=("Reference", "Value"),
             extra={"Function": "J_DOUT", "Pinout": "1=+5V 2=GND 3=DATA_OUT",
                    "Note": "3-pin WAGO — power + data out"})
    q1, q2, q3 = (j2x - 5.08, j2y - 2.54), (j2x - 5.08, j2y), (j2x - 5.08, j2y + 2.54)
    sh.wire(*q1, 35 * U, q1[1]); sh.wire(35 * U, q1[1], 35 * U, 104 * U)
    sh.p5v(35 * U, 104 * U)
    sh.wire(*q2, 29 * U, q2[1]); sh.wire(29 * U, q2[1], 29 * U, 126 * U)
    sh.gnd(29 * U, 126 * U)
    sh.wire(*q3, 40 * U, q3[1]); sh.wire(40 * U, q3[1], 40 * U, 129 * U)
    sh.label("DATA_OUT", 40 * U, 129 * U, 270)
    sh.text("J2  J_DOUT  (1:+5V  2:GND  3:DATA OUT)  — power entry", 44 * U,
            109 * U, 1.8)

    # bulk electrolytic at the J2 power entry
    cx2 = 92 * U
    sh.place(CBULK_LIB, cx2, j2y, f"C{nled + 2}", value=CBULK_VAL,
             footprint=CBULK_FP, show=("Reference", "Value"),
             field_at=PASSIVE_FIELDS)
    sh.wire(cx2, j2y - 3 * U, cx2, 104 * U); sh.p5v(cx2, 104 * U)
    sh.wire(cx2, j2y + 3 * U, cx2, 126 * U); sh.gnd(cx2, 126 * U)
    sh.text("bulk at J2", cx2 + 3 * U, j2y + 6 * U, 1.5)

    # ------------------------------------------------------- PWR_FLAG pair
    sh.p5v(140 * U, IFACE_Y)
    sh.wire(140 * U, IFACE_Y, 152 * U, IFACE_Y)
    sh.pwr_flag(152 * U, IFACE_Y)
    sh.gnd(140 * U, IFACE_Y + 22 * U)
    sh.wire(140 * U, IFACE_Y + 22 * U, 152 * U, IFACE_Y + 22 * U)
    sh.pwr_flag(152 * U, IFACE_Y + 22 * U)
    sh.text("ERC power sources", 138 * U, 54 * U, 1.8)

    # --------------------------------------------- distributed bulk 22 uF
    sh.text(f"DISTRIBUTED BULK — {ndist}x, spread ~1 per 30 LEDs along the array",
            188 * U, 54 * U, 1.8)
    for i in range(ndist):
        cref = f"C{nled + nbulk + 1 + i}"
        cx = (196 + 28 * i) * U
        sh.place(CDIST_LIB, cx, IFACE_Y, cref, value=CDIST_VAL,
                 footprint=CDIST_FP, show=("Reference", "Value"),
                 field_at=PASSIVE_FIELDS)
        sh.wire(cx, IFACE_Y - 3 * U, cx, 49 * U); sh.p5v(cx, 49 * U)
        sh.wire(cx, IFACE_Y + 3 * U, cx, 73 * U); sh.gnd(cx, 73 * U)
        near = min(nled, (i + 1) * 30)
        sh.text(f"near LED{near}", cx - 3 * U, 78 * U, 1.5)

    # ------------------------------------------------------------- the array
    ncol = NCOL[letter]
    sh.text(f"PIXEL CHAIN — LED1 .. LED{nled} in placement-CSV order "
            f"(reads left to right, top to bottom; data is carried by the "
            f"D1..D{nled} labels, not by adjacency)",
            ARRAY_X0 - 16 * U, ARRAY_Y0 - 20 * U, 2.2, True)

    for idx, row in enumerate(rows):
        col, r = idx % ncol, idx // ncol
        x = ARRAY_X0 + col * COL_PITCH
        y = ARRAY_Y0 + r * ROW_PITCH
        k = idx + 1

        sh.place(LED_LIB, x, y, f"LED{k}", value=LED_VAL, footprint=LED_FP,
                 datasheet=LED_DS,
                 extra={"MPN": LED_MPN, "LCSC": LED_LCSC,
                        "Universe": row["Universe"], "Channel": row["Channel"],
                        "Rot_deg": row["Rot_deg"],
                        "PosX_mm": row["PosX_mm"], "PosY_mm": row["PosY_mm"]})
        # power stubs to the row rails
        sh.wire(x, y - 7.62, x, y - RAIL_DY)
        sh.wire(x, y + 7.62, x, y + RAIL_DY)
        sh.junction(x, y - RAIL_DY)
        sh.junction(x, y + RAIL_DY)
        # data stubs + labels
        sh.wire(x - 7.62, y, x - LBL_DX, y)
        sh.label(f"D{k}", x - LBL_DX, y, 180)
        sh.wire(x + 7.62, y, x + LBL_DX, y)
        sh.label("DATA_OUT" if k == nled else f"D{k + 1}", x + LBL_DX, y, 0)
        # decoupling cap
        cx = x + CAP_DX
        sh.place(C_LIB, cx, y, f"C{k}", value=C_VAL, footprint=C_FP,
                 show=("Reference", "Value"),
                 field_at=PASSIVE_FIELDS,
                 extra={"LCSC": C_LCSC, "Pairs_with": f"LED{k}"})
        sh.wire(cx, y - 3.81, cx, y - RAIL_DY)
        sh.wire(cx, y + 3.81, cx, y + RAIL_DY)
        if col != ncol - 1 and idx != nled - 1:
            # the last cap of a row is the rail's end point, not a T
            sh.junction(cx, y - RAIL_DY)
            sh.junction(cx, y + RAIL_DY)

    # per-row +5V / GND rails
    for r in range((nled + ncol - 1) // ncol):
        first = r * ncol
        last = min(first + ncol, nled) - 1
        y = ARRAY_Y0 + r * ROW_PITCH
        x_lo = ARRAY_X0 - 12 * U
        # the rail terminates on the last cap's stub, so neither end dangles
        x_hi = ARRAY_X0 + (last - first) * COL_PITCH + CAP_DX
        sh.wire(x_lo, y - RAIL_DY, x_hi, y - RAIL_DY)
        sh.p5v(x_lo, y - RAIL_DY)
        sh.wire(x_lo, y + RAIL_DY, x_hi, y + RAIL_DY)
        sh.gnd(x_lo, y + RAIL_DY)
        sh.text(f"row {r + 1}: LED{first + 1}..LED{last + 1}",
                x_hi + 4 * U, y, 1.8)

    return sh


# ================================================================== project
PRO_TEMPLATE = {
    "board": {"3dviewports": [], "design_settings": {}, "ipc2581": {},
              "layer_presets": [], "viewports": []},
    "boards": [],
    "cvpcb": {"equivalence_files": []},
    "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
    "meta": {"filename": "", "version": 3},
    "net_settings": {
        "classes": [
            {"bus_width": 12, "clearance": 0.2, "diff_pair_gap": 0.25,
             "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2,
             "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1,
             "name": "Default", "pcb_color": "rgba(0, 0, 0, 0.000)",
             "priority": 2147483647,
             "schematic_color": "rgba(0, 0, 0, 0.000)", "track_width": 0.25,
             "via_diameter": 0.6, "via_drill": 0.3, "wire_width": 6},
            {"bus_width": 12, "clearance": 0.25, "diff_pair_gap": 0.25,
             "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2,
             "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1,
             "name": "DATA", "pcb_color": "rgba(0, 0, 0, 0.000)",
             "priority": 1, "schematic_color": "rgba(0, 0, 0, 0.000)",
             "track_width": 0.25, "via_diameter": 0.6, "via_drill": 0.3,
             "wire_width": 6},
            {"bus_width": 12, "clearance": 0.3, "diff_pair_gap": 0.25,
             "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2,
             "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1,
             "name": "POWER", "pcb_color": "rgba(0, 0, 0, 0.000)",
             "priority": 0, "schematic_color": "rgba(0, 0, 0, 0.000)",
             "track_width": 2.5, "via_diameter": 0.8, "via_drill": 0.4,
             "wire_width": 6},
        ],
        "meta": {"version": 5},
        "net_colors": None,
        "netclass_assignments": None,
        "netclass_patterns": [
            {"netclass": "DATA", "pattern": "D?*"},
            {"netclass": "DATA", "pattern": "DATA_*"},
            {"netclass": "POWER", "pattern": "+5V"},
            {"netclass": "POWER", "pattern": "GND"},
        ],
    },
    "pcbnew": {"last_paths": {}, "page_layout_descr_file": ""},
    "schematic": {
        "annotate_start_num": 0,
        "bom_export_filename": "${PROJECTNAME}-bom.csv",
        "drawing": {"dashed_lines_dash_length_ratio": 12.0,
                    "dashed_lines_gap_length_ratio": 3.0,
                    "default_line_thickness": 6.0,
                    "default_text_size": 50.0,
                    "field_names": [],
                    "intersheets_ref_own_page": False,
                    "intersheets_ref_prefix": "",
                    "intersheets_ref_short": False,
                    "intersheets_ref_show": False,
                    "intersheets_ref_suffix": "",
                    "junction_size_choice": 3,
                    "label_size_ratio": 0.375,
                    "operating_point_overlay_i_precision": 3,
                    "operating_point_overlay_i_range": "~A",
                    "operating_point_overlay_v_precision": 3,
                    "operating_point_overlay_v_range": "~V",
                    "overbar_offset_ratio": 1.23,
                    "pin_symbol_size": 25.0,
                    "text_offset_ratio": 0.15},
        "legacy_lib_dir": "",
        "legacy_lib_list": [],
        "meta": {"version": 1},
        "net_format_name": "",
        "page_layout_descr_file": "",
        "plot_directory": "",
        "spice_current_sheet_as_root": False,
        "spice_external_command": "spice \"%I\"",
        "spice_model_current_sheet_as_root": True,
        "spice_save_all_currents": False,
        "spice_save_all_dissipations": False,
        "spice_save_all_voltages": False,
        "subpart_first_id": 65,
        "subpart_id_separator": 0,
    },
    "sheets": [],
    "text_variables": {},
}


def write_project(outdir: Path, project: str, sheet: Sheet):
    outdir.mkdir(parents=True, exist_ok=True)
    pro = json.loads(json.dumps(PRO_TEMPLATE))
    pro["meta"]["filename"] = f"{project}.kicad_pro"
    pro["sheets"] = [[sheet.root_uuid, "Root"]]
    (outdir / f"{project}.kicad_pro").write_text(json.dumps(pro, indent=2) + "\n")
    (outdir / f"{project}.kicad_sch").write_text(sheet.render())


def main():
    design = json.loads((PACK / "docs" / "design.json").read_text())
    for meta in design["designs"]:
        letter = meta["letter"]
        csv_path = PACK / meta["files"]["placementCsv"]
        with csv_path.open() as fh:
            rows = list(csv.DictReader(fh))
        sheet = build(letter, meta, rows)
        project = f"sun-pcb-design-{letter}"
        write_project(REPO / project, project, sheet)
        print(f"design {letter}: {len(rows)} LEDs -> {project}/")


if __name__ == "__main__":
    main()
