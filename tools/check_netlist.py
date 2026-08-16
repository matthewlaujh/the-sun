#!/usr/bin/env python3
"""Verify the generated schematics: parse the KiCad netlist export and assert
the pixel chain, power nets and part counts match the design pack."""

from __future__ import annotations

import csv
import json
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PACK = REPO / "sun-pcb-kicad-pack"
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"


# ------------------------------------------------------------- tiny s-expr
TOKEN = re.compile(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+')


def parse(text: str):
    stack, cur = [], []
    for tok in TOKEN.findall(text):
        if tok == "(":
            stack.append(cur)
            cur = []
        elif tok == ")":
            done, cur = cur, stack.pop()
            cur.append(done)
        elif tok.startswith('"'):
            cur.append(tok[1:-1].encode().decode("unicode_escape"))
        else:
            cur.append(tok)
    return cur[0]


def kids(node, tag):
    return [c for c in node if isinstance(c, list) and c and c[0] == tag]


def kid(node, tag):
    got = kids(node, tag)
    return got[0] if got else None


def main() -> int:
    design = json.loads((PACK / "docs" / "design.json").read_text())
    bad = 0
    for meta in design["designs"]:
        L = meta["letter"]
        proj = REPO / f"sun-pcb-design-{L}"
        sch = proj / f"sun-pcb-design-{L}.kicad_sch"
        with tempfile.NamedTemporaryFile(suffix=".net") as tf:
            subprocess.run(
                [KICAD_CLI, "sch", "export", "netlist", "--format", "kicadsexpr",
                 "-o", tf.name, str(sch)],
                check=True, capture_output=True)
            root = parse(Path(tf.name).read_text())

        comps = {}
        for c in kids(kid(root, "components"), "comp"):
            ref = kid(c, "ref")[1]
            comps[ref] = {"value": kid(c, "value")[1],
                          "fp": (kid(c, "footprint") or [None, ""])[1]}
        nets = {}
        for nt in kids(kid(root, "nets"), "net"):
            name = kid(nt, "name")[1].lstrip("/")
            nets[name] = {(kid(nd, "ref")[1], kid(nd, "pin")[1])
                          for nd in kids(nt, "node")}

        n = meta["leds"]
        nbulk = 2   # one bulk electrolytic at each of the two WAGO connectors
        with (PACK / meta["files"]["placementCsv"]).open() as fh:
            rows = list(csv.DictReader(fh))
        errs = []

        # 1. chain: Dk joins LED(k-1).DOUT(4) -> LEDk.DIN(2); D1 comes from R1
        for k in range(1, n + 1):
            want = {(f"LED{k}", "2")}
            want |= {("R1", "2")} if k == 1 else {(f"LED{k-1}", "4")}
            if nets.get(f"D{k}") != want:
                errs.append(f"net D{k}: {sorted(nets.get(f'D{k}', []))}")
        if nets.get("DATA_IN") != {("J1", "3"), ("R1", "1")}:
            errs.append(f"DATA_IN: {sorted(nets.get('DATA_IN', []))}")
        if nets.get("DATA_OUT") != {(f"LED{n}", "4"), ("J2", "3")}:
            errs.append(f"DATA_OUT: {sorted(nets.get('DATA_OUT', []))}")

        # 2. every LED and every cap on the rails, plus both 3-pin connectors
        v5, gnd = nets.get("+5V", set()), nets.get("GND", set())
        ncaps = n + nbulk + max(1, round(n / 30))
        want5 = {(f"LED{k}", "3") for k in range(1, n + 1)}
        want5 |= {(f"C{k}", "1") for k in range(1, ncaps + 1)}
        want5 |= {("J1", "1"), ("J2", "1")}
        wantg = {(f"LED{k}", "1") for k in range(1, n + 1)}
        wantg |= {(f"C{k}", "2") for k in range(1, ncaps + 1)}
        wantg |= {("J1", "2"), ("J2", "2")}
        if not want5 <= v5:
            errs.append(f"+5V missing {sorted(want5 - v5)[:6]}")
        if not wantg <= gnd:
            errs.append(f"GND missing {sorted(wantg - gnd)[:6]}")

        # 3. no stray nets
        stray = [k for k in nets
                 if not re.fullmatch(r"D\d+|DATA_IN|DATA_OUT|\+5V|GND", k)]
        if stray:
            errs.append(f"unexpected nets: {stray[:8]}")

        # 4. component inventory
        pref = Counter(re.match(r"[A-Za-z#]+", r).group(0) for r in comps)
        want_pref = {"LED": n, "C": ncaps, "R": 1, "J": 2}
        for p, q in want_pref.items():
            if pref.get(p) != q:
                errs.append(f"{p}: {pref.get(p)} parts, expected {q}")

        # 5. LED value tracks the placement CSV row by row
        for i, row in enumerate(rows, 1):
            led = comps.get(f"LED{i}")
            if led is None or led["value"] != row["Val"]:
                errs.append(f"LED{i} value {led and led['value']} != {row['Val']}")
                break

        # 6. every passive value follows <package>/<value>/<voltage>
        for ref, c in comps.items():
            if ref[0] not in "CR":
                continue
            if not re.fullmatch(r"[^/\s]+/[^/\s]+/\d+V", c["value"]):
                errs.append(f"{ref} value {c['value']!r} breaks the "
                            f"<package>/<value>/<voltage> standard")

        print(f"design {L}: {len(comps)} parts "
              f"({pref.get('LED')} LED, {pref.get('C')} C, {pref.get('R')} R, "
              f"{pref.get('J')} J, {pref.get('#PWR', 0)+pref.get('#FLG', 0)} power syms), "
              f"{len(nets)} nets -> " + ("OK" if not errs else "FAIL"))
        for e in errs[:10]:
            print("   !", e)
        bad += len(errs)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
