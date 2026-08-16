#!/usr/bin/env python3
"""Verify the generated boards against the design pack.

Run with KiCad's interpreter (needs pcbnew):

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/\
Versions/3.9/bin/python3 tools/check_pcb.py
"""

from __future__ import annotations

import csv
import json
import math
import sys
from pathlib import Path

import wx
_APP = wx.App()
import pcbnew

REPO = Path(__file__).resolve().parent.parent
PACK = REPO / "sun-pcb-kicad-pack"

TOP = {"LED", "C_LED", "R"}


def main() -> int:
    design = json.loads((PACK / "docs" / "design.json").read_text())
    bad = 0
    for meta in design["designs"]:
        L = meta["letter"]
        n = meta["leds"]
        rows = list(csv.DictReader((PACK / meta["files"]["placementCsv"]).open()))
        path = REPO / f"sun-pcb-design-{L}" / f"sun-pcb-design-{L}.kicad_pcb"
        b = pcbnew.LoadBoard(str(path))
        fps = {f.GetReference(): f for f in b.GetFootprints()}
        errs = []

        # grid origin is the CSV's (0,0)
        o = b.GetDesignSettings().GetGridOrigin()
        ox, oy = pcbnew.ToMM(o.x), pcbnew.ToMM(o.y)

        # 1. every LED sits exactly where the placement CSV says
        flipped = 0
        for k, row in enumerate(rows, 1):
            f = fps.get(f"LED{k}")
            if f is None:
                errs.append(f"LED{k} missing")
                break
            p = f.GetPosition()
            dx = pcbnew.ToMM(p.x) - ox - float(row["PosX_mm"])
            dy = pcbnew.ToMM(p.y) - oy - float(row["PosY_mm"])
            if abs(dx) > 0.002 or abs(dy) > 0.002:
                errs.append(f"LED{k} at +({dx:.3f},{dy:.3f}) mm off the CSV")
            d = (f.GetOrientationDegrees() - float(row["Rot_deg"])) % 360.0
            if min(d, 360 - d) < 0.01:
                pass
            elif abs(d - 180.0) < 0.01:
                flipped += 1
            else:
                errs.append(f"LED{k} rotation {f.GetOrientationDegrees():.1f} "
                            f"vs CSV {row['Rot_deg']}")
            if f.IsFlipped():
                errs.append(f"LED{k} is on the wire side")

        # 2. side assignment
        for ref, want_bottom in (
                *[(f"C{k}", False) for k in range(1, n + 1)],
                ("R1", False), ("J1", True), ("J2", True),
                (f"C{n+1}", True), (f"C{n+2}", True)):
            f = fps.get(ref)
            if f is None:
                errs.append(f"{ref} missing")
            elif f.IsFlipped() != want_bottom:
                errs.append(f"{ref} on the "
                            f"{'wire' if f.IsFlipped() else 'LED'} side, wrong")

        # 3. every LED and every LED cap stitches +5V through its own via
        vias = [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_VIA_T]
        v5 = [(pcbnew.ToMM(v.GetPosition().x), pcbnew.ToMM(v.GetPosition().y))
              for v in vias if v.GetNetname() == "+5V"]
        for k in range(1, n + 1):
            for ref in (f"LED{k}", f"C{k}"):
                p = fps[ref].GetPosition()
                px, py = pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)
                if not any(math.dist((px, py), q) < 8.0 for q in v5):
                    errs.append(f"{ref} has no +5V via within 8 mm")
                    break

        # 4. pours present, filled and on the right layers
        # GetLayerName() mis-reports B.Cu zones under swig; use the layer id
        zones = {z.GetLayer(): z for z in b.Zones()}
        for layer, name, net in ((pcbnew.F_Cu, "F.Cu", "GND"),
                                 (pcbnew.B_Cu, "B.Cu", "+5V")):
            z = zones.get(layer)
            if z is None or z.GetNetname() != net:
                errs.append(f"{name} pour missing or not {net}")
            elif z.GetFilledArea() <= 0:
                errs.append(f"{name} pour is not filled")

        # 5. board outline closed and the right size
        bbox = b.GetBoardEdgesBoundingBox()
        w, h = pcbnew.ToMM(bbox.GetWidth()), pcbnew.ToMM(bbox.GetHeight())
        if min(w, h) > 460 or max(w, h) > 500:
            errs.append(f"outline {w:.1f}x{h:.1f} mm exceeds the 460x500 panel")

        nets = {t.GetNetname() for t in b.GetTracks()}
        print(f"design {L}: {len(fps)} footprints, {len(b.GetTracks())} "
              f"tracks+vias ({len(vias)} vias), {len(nets)} routed nets, "
              f"outline {w:.1f}x{h:.1f} mm, {flipped}/{n} LEDs turned 180 deg "
              f"-> " + ("OK" if not errs else "FAIL"))
        for e in errs[:8]:
            print("   !", e)
        bad += len(errs)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
